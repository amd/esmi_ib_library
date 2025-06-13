/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright (c) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 */

#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>

#include <e_smi/e_smi.h>

#define RED "\x1b[31m"
#define MAG "\x1b[35m"
#define RESET "\x1b[0m"
#include <e_smi/e_smi64Config.h>

#define ARGS_MAX 64
#define SHOWLINESZ 256

/*
 * To handle multiple errors while reporting tool summary.
 * 1234 is just chosen to make this value different than other error codes
 */
#define ESMI_MULTI_ERROR	1234
#define COLS			3
#define AID_COUNT		4
#define XCC_COUNT		8
#define NUM_XGMI_LINKS		8

/* Total bits required to represent integer */
#define NUM_OF_32BITS		(sizeof(uint32_t) * 8)
#define NUM_OF_64BITS		(sizeof(uint64_t) * 8)
#define KILO			pow(10,3)

static const char *bw_string[3] = {"aggregate", "read", "write"}; //!< bandwidth types for io/xgmi links

static int flag;
static double initial_delay_in_secs = 0;
static double loop_delay_in_secs    = 0;
static bool loop_delay_in_secs_passed = false;
static int loop_count		    = 0;
static char* log_file_name   = NULL;
static char* stoploop_file_name   = NULL;
static char* log_file_header = NULL;
static char* log_file_data   = NULL;
static bool create_log_header = false;

typedef enum {
	DONT_PRINT_RESULTS = 0,
	PRINT_RESULTS,
	PRINT_RESULTS_AS_CSV,
	PRINT_RESULTS_AS_JSON
}print_results_t;
static print_results_t print_results = PRINT_RESULTS;

typedef enum {
	DONT_LOG_TO_FILE = 0,
	LOG_TO_FILE
}log_to_file_t;
static log_to_file_t log_to_file = DONT_LOG_TO_FILE;

static struct epyc_sys_info {
	uint32_t sockets;
	uint32_t cpus;
	uint32_t threads_per_core;
	uint32_t family;
	uint32_t model;
	void (*show_addon_cpu_metrics)(uint32_t *);
	void (*show_addon_socket_metrics)(uint32_t *, char **);
	void (*show_addon_clock_metrics)(uint32_t *, char **);
} sys_info;

static void sleep_in_milliseconds(unsigned milliseconds)
{
	usleep(milliseconds * 1000); // takes microseconds
}

void append_string(char **buffer, const char *new_str) {
	size_t current_length = *buffer ? strlen(*buffer) : 0;
	size_t new_str_length = strlen(new_str);
	size_t new_length = current_length + new_str_length + 1; // +1 for null terminator

	// Reallocate buffer to the new size
	char *new_buffer = realloc(*buffer, new_length);
	if (new_buffer == NULL) {
		return;
	}

	// Append the new string
	strcpy(new_buffer + current_length, new_str);

	// Update the buffer pointer
	*buffer = new_buffer;
}

static void print_socket_footer()
{
	if(print_results != PRINT_RESULTS) return;
	int i;

	printf("\n----------------------------------");
	for (i = 0; i < sys_info.sockets; i++) {
		printf("-------------------");
	}
}

static void print_socket_header()
{
	if(print_results != PRINT_RESULTS) return;
	int i;

	print_socket_footer();
	printf("\n| Sensor Name\t\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		printf(" Socket %-10d|", i);
	}
	print_socket_footer();
}

static void err_bits_print(uint32_t err_bits)
{
	if(print_results != PRINT_RESULTS) return;
	int i;

	printf("\n");
	for (i = 1; i < 32; i++) {
		if (err_bits & (1 << i))
			printf(RED "Err[%d]: %s\n" RESET, i, esmi_get_err_msg(i));
	}
}

#define ALLOWLIST_FILE "/dev/cpu/msr_allowlist"
static char *allowlistcontent = "# MSR # Write Mask # Comment\n"
			  "0xC0010299 0x0000000000000000\n"
			  "0xC001029A 0x0000000000000000\n"
			  "0xC001029B 0x0000000000000000\n"
			  "0xC00102F0 0x0000000000000000\n"
			  "0xC00102F1 0x0000000000000000\n";

static int write_msr_allowlist_file()
{
	int fd;
	int ret;

	if (!access(ALLOWLIST_FILE, F_OK)) {
		fd = open(ALLOWLIST_FILE, O_RDWR);
		if (fd < 0) {
			printf("Error in opening msr allowlist: %s\n", strerror(errno));
			return errno;
		}
		ret = write(fd, allowlistcontent, strlen(allowlistcontent));
		if (ret < 0) {
			printf("Error in writing msr allowlist: %s\n", strerror(errno));
			return errno;
		}

		close(fd);
	}
	printf("Successfully added msr allowlist.\n");

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_get_coreenergy(uint32_t core_id)
{
	esmi_status_t ret;
	uint64_t core_input = 0;

	ret = esmi_core_energy_get(core_id, &core_input);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get core[%d] energy, Err[%d]: %s\n",
			core_id, ret, esmi_get_err_msg(ret));
		if (ret == ESMI_PERMISSION) {
			printf(RED "\nTry adding msr allowlist using "
				"--writemsrallowlist tool option.\n" RESET);
		}
		return ret;
	}

	if(print_results == PRINT_RESULTS) {
		printf("-------------------------------------------------");
		printf("\n| core[%d] energy  | %17.3lf Joules \t|\n",
			core_id, (double)core_input/1000000);
		printf("-------------------------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Core,CoreEnergy(Joules)\n%d,%.3f\n", core_id, (double)core_input/1000000);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"Core\":%d,\n\t\t\"CoreEnergy(Joules)\":%.3f\n\t},", core_id, (double)core_input/1000000);

	if(log_to_file) {
		char temp_string[300];
		if(create_log_header) {
			sprintf(temp_string, "Core%d:CoreEnergy(Joules),", core_id);
			append_string(&log_file_header, temp_string);
		}
		sprintf(temp_string, "%.3f,", (double)core_input/1000000);
		append_string(&log_file_data, temp_string);
	}
	return ESMI_SUCCESS;
}

static int epyc_get_sockenergy(void)
{
	esmi_status_t ret;
	uint32_t i;
	uint64_t pkg_input = 0;
	uint32_t err_bits = 0;

	print_socket_header();
	if(print_results == PRINT_RESULTS)
		printf("\n| Energy (K Joules)\t\t |");

	for (i = 0; i < sys_info.sockets; i++) {
		char temp_string[300];
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%d:SocketEnergy(KJoules),", i);
			append_string(&log_file_header, temp_string);
		}

		ret = esmi_socket_energy_get(i, &pkg_input);
		if (!ret) {
			if(print_results == PRINT_RESULTS)
				printf(" %-17.3lf|", (double)pkg_input/1000000000);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,SocketEnergy(KJoules)\n%d,%.3f\n", i, (double)pkg_input/1000000000);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"SocketEnergy(KJoules)\":%.3f\n\t},", i, (double)pkg_input/1000000000);

			if(log_to_file) {
				sprintf(temp_string, "%.3f,", (double)pkg_input/1000000000);
				append_string(&log_file_data, temp_string);
			}
		} else {
			err_bits |= 1 << ret;
			if(print_results == PRINT_RESULTS)
				printf(" NA (Err: %-2d)     |", ret);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,SocketEnergy(KJoules)\n%d,NA (Err:%d)\n", i, ret);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"SocketEnergy(KJoules)\":\"NA (Err:%d)\"\n\t},", i, ret);

			if(log_to_file) {
				sprintf(temp_string, "NA (Err:%d),", ret);
				append_string(&log_file_data, temp_string);
			}
		}
	}
	print_socket_footer();
	if(print_results == PRINT_RESULTS)
		printf("\n");
	err_bits_print(err_bits);

	if ((err_bits >> ESMI_PERMISSION) & 0x1)
		printf(RED "\nTry adding msr allowlist using "
			"--writemsrallowlist tool option.\n" RESET);
	if (err_bits > 1)
		return ESMI_MULTI_ERROR;

	return ESMI_SUCCESS;
}

static void ddr_bw_get(uint32_t *err_bits)
{
	struct ddr_bw_metrics ddr;
	char bw_str[SHOWLINESZ] = {};
	char pct_str[SHOWLINESZ] = {};
	char max_str[SHOWLINESZ] = {};
	uint32_t bw_len;
	uint32_t pct_len;
	uint32_t i, ret;
	uint32_t max_len;

	if(print_results == PRINT_RESULTS)
		printf("\n| DDR Bandwidth\t\t\t |");

	snprintf(max_str, SHOWLINESZ, "\n| \tDDR Max BW (GB/s)\t |");
	snprintf(bw_str, SHOWLINESZ, "\n| \tDDR Utilized BW (GB/s)\t |");
	snprintf(pct_str, SHOWLINESZ, "\n| \tDDR Utilized Percent(%%)\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		char temp_string[300];
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%x:DDRMaxBw(GB/s),Socket%x:DDRUtilizedBw(GB/s),Socket%x:DDRUtilizedPercent(%%),", i, i, i);
			append_string(&log_file_header, temp_string);
		}

		if(print_results == PRINT_RESULTS)
			printf("                  |");

		ret = esmi_ddr_bw_get(i, &ddr);
		bw_len = strlen(bw_str);
		pct_len = strlen(pct_str);
		max_len = strlen(max_str);
		if(!ret) {
			snprintf(max_str + max_len, SHOWLINESZ - max_len, " %-17d|", ddr.max_bw);
			snprintf(bw_str + bw_len, SHOWLINESZ - bw_len, " %-17d|", ddr.utilized_bw);
			snprintf(pct_str + pct_len, SHOWLINESZ - pct_len, " %-17d|", ddr.utilized_pct);
			if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,DDRMaxBw(GB/s),DDRUtilizedBw(GB/s),DDRUtilizedPercent(%%)\n%d,%d,%d,%d\n", i, ddr.max_bw, ddr.utilized_bw, ddr.utilized_pct);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"DDRMaxBw(GB/s)\":%d,\n\t\t\"DDRUtilizedBw(GB/s)\":%d,\n\t\t\"DDRUtilizedPercent(%%)\":%d\n\t},", i, ddr.max_bw, ddr.utilized_bw, ddr.utilized_pct);

			if(log_to_file) {
				sprintf(temp_string, "%d,%d,%d,", ddr.max_bw, ddr.utilized_bw, ddr.utilized_pct);
				append_string(&log_file_data, temp_string);
			}
		} else {
			*err_bits |= 1 << ret;
			snprintf(max_str + max_len, SHOWLINESZ - max_len, " NA (Err: %-2d)     |", ret);
			snprintf(bw_str + bw_len, SHOWLINESZ - bw_len, " NA (Err: %-2d)     |", ret);
			snprintf(pct_str + pct_len, SHOWLINESZ - pct_len, " NA (Err: %-2d)     |", ret);
			if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,DDRMaxBw(GB/s),DDRUtilizedBw(GB/s),DDRUtilizedPercent(%%)\n%d,NA (Err:%d),NA (Err:%d),NA (Err:%d)\n", i, ddr.max_bw, ddr.utilized_bw, ddr.utilized_pct);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"DDRMaxBw(GB/s)\":\"NA (Err:%d)\",\n\t\t\"DDRUtilizedBw(GB/s)\":\"NA (Err:%d)\"\n\t\t\"DDRUtilizedPercent(%%)\":\"NA (Err:%d)\"\n\t},", i, ret, ret, ret);

			if(log_to_file) {
				sprintf(temp_string, "NA (Err:%d),NA (Err:%d),NA (Err:%d),", ret, ret, ret);
				append_string(&log_file_data, temp_string);
			}
		}
	}
	if(print_results == PRINT_RESULTS)
	{
		printf("%s", max_str);
		printf("%s", bw_str);
		printf("%s", pct_str);
	}
}

static int epyc_get_ddr_bw(void)
{
	uint32_t err_bits = 0;

	print_socket_header();
	ddr_bw_get(&err_bits);

	print_socket_footer();
	err_bits_print(err_bits);
	if (err_bits > 1)
		return ESMI_MULTI_ERROR;

	return ESMI_SUCCESS;
}

static int epyc_get_temperature(void)
{
	esmi_status_t ret;
	uint32_t i;
	uint32_t tmon = 0;
	uint32_t err_bits = 0;

	print_socket_header();
	if(print_results == PRINT_RESULTS)
		printf("\n| Temperature\t\t\t |");

	for (i = 0; i < sys_info.sockets; i++) {
		char temp_string[300];
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%x:Temperature('C),", i);
			append_string(&log_file_header, temp_string);
		}

		ret = esmi_socket_temperature_get(i, &tmon);
		if (!ret) {
			if(print_results == PRINT_RESULTS)
				printf(" %3.3f°C\t    |", (double)tmon/1000);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,Temperature('C)\n%d,%.3f\n", i, (double)tmon/1000);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"Temperature('C)\":%.3f\n\t},", i, (double)tmon/1000);

			if(log_to_file) {
				sprintf(temp_string, "%3.3f,", (double)tmon/1000);
				append_string(&log_file_data, temp_string);
			}
		} else {
			err_bits |= 1 << ret;
			if(print_results == PRINT_RESULTS)
				printf(" NA (Err: %-2d)     |", ret);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,Temperature('C)\n%d,NA (Err:%d)\n", i, ret);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"Temperature('C)\":\"NA (Err:%d)\"\n\t},", i, ret);

			if(log_to_file) {
				sprintf(temp_string, "NA (Err:%d),",ret);
				append_string(&log_file_data, temp_string);
			}
		}
	}
	print_socket_footer();
	err_bits_print(err_bits);
	if (err_bits > 1)
		return ESMI_MULTI_ERROR;
	return ESMI_SUCCESS;
}

static esmi_status_t epyc_get_smu_fw_version(void)
{
	struct smu_fw_version smu_fw;
	esmi_status_t ret;

	ret = esmi_smu_fw_version_get(&smu_fw);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get SMU Firmware Version, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}

	if(print_results == PRINT_RESULTS)
	{
		printf("\n------------------------------------------");
		printf("\n| SMU FW Version   |  %u.%u.%u \t\t |\n",
			smu_fw.major, smu_fw.minor, smu_fw.debug);
		printf("------------------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Socket,SMUFWversion\n0,%u.%u.%u\n", smu_fw.major, smu_fw.minor, smu_fw.debug);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"Socket\":0,\n\t\t\"SMUFWVersion\":%u.%u.%u\n\t},", smu_fw.major, smu_fw.minor, smu_fw.debug);

	if(log_to_file) {
		char temp_string[300];
		if(create_log_header) {
			sprintf(temp_string, "Socket0:SMUFWVersion,");
			append_string(&log_file_header, temp_string);
		}

		sprintf(temp_string, "%u.%u.%u,", smu_fw.major, smu_fw.minor, smu_fw.debug);
		append_string(&log_file_data, temp_string);
	}

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_get_hsmp_driver_version(void)
{
        struct hsmp_driver_version hsmp_driver_ver;
        esmi_status_t ret;

        ret = esmi_hsmp_driver_version_get(&hsmp_driver_ver);
        if (ret != ESMI_SUCCESS) {
                printf("Failed to get HSMP Driver Version, Err[%d]: %s\n",
                        ret, esmi_get_err_msg(ret));
                return ret;
        }
	if(print_results == PRINT_RESULTS)
	{
			printf("\n------------------------------------------");
			printf("\n| HSMP Driver Version   |  %u.%u \t\t |\n",
					hsmp_driver_ver.major, hsmp_driver_ver.minor);
			printf("------------------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Socket,HSMPDriverVersion\n0,%u.%u\n", hsmp_driver_ver.major, hsmp_driver_ver.minor);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"Socket\":0,\n\t\t\"HSMPDriverVersion\":%u.%u\n\t},", hsmp_driver_ver.major, hsmp_driver_ver.minor);

	if(log_to_file) {
		char temp_string[300];
		if(create_log_header) {
			sprintf(temp_string, "Socket0:HsmpDriverVersion,");
			append_string(&log_file_header, temp_string);
		}

		sprintf(temp_string, "%u.%u,", hsmp_driver_ver.major, hsmp_driver_ver.minor);
		append_string(&log_file_data, temp_string);
	}

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_get_hsmp_proto_version(void)
{
	uint32_t hsmp_proto_ver;
	esmi_status_t ret;

	ret = esmi_hsmp_proto_ver_get(&hsmp_proto_ver);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get hsmp protocol version, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	if(print_results == PRINT_RESULTS)
	{
		printf("\n---------------------------------");
		printf("\n| HSMP Protocol Version  | %u\t|\n", hsmp_proto_ver);
		printf("---------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Socket,HSMPProtocolVersion\n0,%u\n", hsmp_proto_ver);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"Socket\":0,\n\t\t\"HsmpProtocolVersion\":%u\n\t},", hsmp_proto_ver);

	if(log_to_file) {
		char temp_string[300];
		if(create_log_header) {
			sprintf(temp_string, "Socket0:HsmpProtocolVersion,");
			append_string(&log_file_header, temp_string);
		}

		sprintf(temp_string, "%u,", hsmp_proto_ver);
		append_string(&log_file_data, temp_string);
	}

	return ESMI_SUCCESS;
}

static int epyc_get_prochot_status(void)
{
	esmi_status_t ret;
	uint32_t i;
	uint32_t prochot;
	uint32_t err_bits = 0;

	print_socket_header();

	if(print_results == PRINT_RESULTS)
		printf("\n| ProchotStatus:\t\t |");

	for (i = 0; i < sys_info.sockets; i++) {
		char temp_string[300];
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%x:ProchotStatus,", i);
			append_string(&log_file_header, temp_string);
		}

		ret = esmi_prochot_status_get(i, &prochot);
		if (!ret) {
			if(print_results == PRINT_RESULTS)
				printf(" %-17s|", prochot? "active" : "inactive");
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,ProchotStatus\n%d,%s\n", i, prochot? "active" : "inactive");
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"ProchotStatus\":%s\n\t},", i, prochot? "\"active\"" : "\"inactive\"");

			if(log_to_file) {
				sprintf(temp_string, "%s,", prochot? "active" : "inactive");
				append_string(&log_file_data, temp_string);
			}
		} else {
			err_bits |= 1 << ret;
			if(print_results == PRINT_RESULTS)
				printf(" NA (Err: %-2d)     |", ret);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,ProchotStatus\n%d,NA (Err:%d)\n", i, ret);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"ProchotStatus\":\"NA (Err:%d)\"\n\t},", i, ret);

			if(log_to_file) {
				sprintf(temp_string, "NA (Err:%d),", ret);
				append_string(&log_file_data, temp_string);
			}
		}
	}
	print_socket_footer();
	if(print_results == PRINT_RESULTS) printf("\n");
	err_bits_print(err_bits);
	if (err_bits > 1)
		return ESMI_MULTI_ERROR;
	return ESMI_SUCCESS;
}
static bool print_src = false;

static void display_freq_limit_src_names(char **freq_src)
{
	int j = 0;
	int i;

	for (i = 0; i < sys_info.sockets; i++) {
		j = 0;
		char temp_string[300];
		if(print_results == PRINT_RESULTS)
			printf("*%d Frequency limit source names: \n", i);
		else if(print_results == PRINT_RESULTS_AS_CSV)
			printf("Socket,FrequencyLimitSource\n%d,",i);
		else if(print_results == PRINT_RESULTS_AS_JSON)
			printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"FrequencyLimitSource\":\"", i);

		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%d:FrequencyLimitSource,", i);
			append_string(&log_file_header, temp_string);
		}

		while (freq_src[j + (i * ARRAY_SIZE(freqlimitsrcnames))]) {
			if(print_results == PRINT_RESULTS)
				printf(" %s\n", freq_src[j +(i * ARRAY_SIZE(freqlimitsrcnames))]);
			else if((print_results == PRINT_RESULTS_AS_CSV) || (print_results == PRINT_RESULTS_AS_JSON))
				printf("%s:", freq_src[j +(i * ARRAY_SIZE(freqlimitsrcnames))]);

			if(log_to_file)
			{
				sprintf(temp_string, "%s:", freq_src[j +(i * ARRAY_SIZE(freqlimitsrcnames))]);
				append_string(&log_file_data, temp_string);
			}
			j++;
		}
		if (j == 0)
		{
			if(print_results == PRINT_RESULTS)
				printf(" %s\n", "Reserved");
			else if((print_results == PRINT_RESULTS_AS_CSV) || (print_results == PRINT_RESULTS_AS_JSON))
				printf("%s:", "Reserved");

			if(log_to_file)
			{
				sprintf(temp_string, "%s:", "Reserved");
				append_string(&log_file_data, temp_string);
			}
		}
		if((print_results == PRINT_RESULTS) || (print_results == PRINT_RESULTS_AS_CSV))
			printf("\n");
		else if(print_results == PRINT_RESULTS_AS_JSON)
			printf("\"\n\t},");

		if(log_to_file)
		{
			sprintf(temp_string, "%s", ",");
			append_string(&log_file_data, temp_string);
		}
	}

}

static void get_sock_freq_limit(uint32_t *err_bits, char **freq_src)
{
	char str1[SHOWLINESZ];
	char str2[SHOWLINESZ];
	int len2, len1;
	uint16_t limit;
	int ret;
	int i;
	int size = ARRAY_SIZE(freqlimitsrcnames);

	if(print_results == PRINT_RESULTS)
		printf("\n| Current Active Freq limit\t |");

	snprintf(str1, SHOWLINESZ, "\n| \t Freq limit (MHz) \t |");
	snprintf(str2, SHOWLINESZ, "\n| \t Freq limit source \t |");

	for (i = 0; i < sys_info.sockets; i++) {
		char temp_string[300];
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%d:ActiveFrequencyLimit(MHz),", i);
			append_string(&log_file_header, temp_string);
		}

		len1 = strlen(str1);
		len2 = strlen(str2);
		if(print_results == PRINT_RESULTS) printf("                  |");
		ret = esmi_socket_current_active_freq_limit_get(i, &limit, freq_src + (i * size));
		if (!ret) {
			snprintf(str1 + len1, SHOWLINESZ - len1, " %-17u|", limit);
			snprintf(str2 + len2, SHOWLINESZ - len2, " Refer below[*%d]  |", i);

			if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,ActiveFrequencyLimit(MHz)\n%d,%u\n", i, limit);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"ActiveFrequencyLimit(MHz)\":%u\n\t},", i, limit);

			if(log_to_file)
			{
				sprintf(temp_string, "%u,", limit);
				append_string(&log_file_data, temp_string);
			}
		} else {
			*err_bits |= 1 << ret;
			snprintf(str1 + len1, SHOWLINESZ - len1, " NA (Err: %-2d)     |", ret);
			snprintf(str2 + len2, SHOWLINESZ - len2, " NA (Err: %-2d)     |", ret);

			if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,ActiveFrequencyLimit(MHz)\n%d,NA (Err:%d)\n", i, ret);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"ActiveFrequencyLimit(MHz)\":\"NA (Err:%d)\"\n\t},", i, ret);

			if(log_to_file)
			{
				sprintf(temp_string, "NA (Err:%d),", ret);
				append_string(&log_file_data, temp_string);
			}
		}
	}
	if(print_results == PRINT_RESULTS)
	{
		printf("%s", str1);
		printf("%s", str2);
	}
	print_src = true;
}

static void get_sock_freq_range(uint32_t *err_bits)
{
	char str1[SHOWLINESZ];
	char str2[SHOWLINESZ];
	int len2, len1;
	uint16_t fmax;
	uint16_t fmin;
	int ret, i;

	if(print_results == PRINT_RESULTS)
		printf("\n| Socket frequency range\t |");

	snprintf(str1, SHOWLINESZ, "\n| \t Fmax (MHz)\t\t |");
	snprintf(str2, SHOWLINESZ, "\n| \t Fmin (MHz)\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		char temp_string[300];
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%d:Fmax(MHz),Socket%d:Fmin(MHz),", i, i);
			append_string(&log_file_header, temp_string);
		}

		if(print_results == PRINT_RESULTS) printf("                  |");
		len1 = strlen(str1);
		len2 = strlen(str2);
		ret = esmi_socket_freq_range_get(i, &fmax, &fmin);
		if (!ret) {
			snprintf(str1 + len1, SHOWLINESZ -len1, " %-17u|", fmax);
			snprintf(str2 + len2, SHOWLINESZ - len2 , " %-17u|", fmin);

			if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,Fmax(MHz),Fmin(MHz)\n%d,%u,%u\n", i, fmax, fmin);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"Fmax(MHz)\":%u,\n\t\t\"Fmin(MHz)\":%u\n\t},", i, fmax, fmin);

			if(log_to_file)
			{
				sprintf(temp_string, "%u,%u,", fmax,fmin);
				append_string(&log_file_data, temp_string);
			}
		} else {
			*err_bits |= 1 << ret;
			snprintf(str1 + len1, SHOWLINESZ -len1, " NA (Err: %-2d)     |", ret);
			snprintf(str2 + len2, SHOWLINESZ - len2, " NA (Err: %-2d)     |", ret);

			if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,Fmax(MHz),Fmin(MHz)\n%d,NA (Err:%d),NA (Err:%d)\n", i, ret, ret);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"Fmax(MHz)\":\"NA (Err:%d)\",\n\t\t\"Fmin(MHz)\":\"NA (Err:%d)\"\n\t},", i, ret, ret);

			if(log_to_file)
			{
				sprintf(temp_string, "NA (Err:%d),NA (Err:%d),", ret, ret);
				append_string(&log_file_data, temp_string);
			}
		}
	}
	if(print_results == PRINT_RESULTS)
	{
		printf("%s", str1);
		printf("%s", str2);
	}
}

static int epyc_get_clock_freq(void)
{
	esmi_status_t ret;
	uint32_t i;
	uint32_t fclk, mclk, cclk;
	char str[SHOWLINESZ] = {};
	uint32_t len;
	uint32_t err_bits = 0;
	char *freq_src[ARRAY_SIZE(freqlimitsrcnames) * sys_info.sockets];

	for (i = 0; i < (ARRAY_SIZE(freqlimitsrcnames) * sys_info.sockets); i++)
		freq_src[i] = NULL;

	print_socket_header();
	if(print_results == PRINT_RESULTS)
		printf("\n| fclk (Mhz)\t\t\t |");

	snprintf(str, SHOWLINESZ, "\n| mclk (Mhz)\t\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		char temp_string[300];
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%d:Fclk(MHz),Socket%d:Mclk(MHz),", i, i);
			append_string(&log_file_header, temp_string);
		}
		len = strlen(str);
		ret = esmi_fclk_mclk_get(i, &fclk, &mclk);
		if (!ret) {
			if(print_results == PRINT_RESULTS)
				printf(" %-17d|", fclk);
			snprintf(str + len, SHOWLINESZ - len, " %-17d|", mclk);

			if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,Fclk(MHz),Mclk(MHz)\n%d,%d,%d\n", i, fclk, mclk);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"Fclk(MHz)\":%d,\n\t\t\"Mclk(MHz)\":%d\n\t},", i, fclk, mclk);

			if(log_to_file)
			{
				sprintf(temp_string, "%d,%d,", fclk, mclk);
				append_string(&log_file_data, temp_string);
			}
		} else {
			err_bits |= 1 << ret;
			if(print_results == PRINT_RESULTS)
				printf(" NA (Err: %-2d)     |", ret);
			snprintf(str + len, SHOWLINESZ - len, " NA (Err: %-2d)     |", ret);

			if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,Fclk(MHz),Mclk(MHz)\n%d,NA (Err:%d),NA (Err:%d)\n", i, ret, ret);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"Fclk(MHz)\":\"NA (Err:%d)\",\n\t\t\"Mclk(MHz)\":\"NA (Err:%d)\"\n\t},", i, ret, ret);

			if(log_to_file)
			{
				sprintf(temp_string, "NA (Err:%d),NA (Err:%d),", ret, ret);
				append_string(&log_file_data, temp_string);
			}
		}
	}
	if(print_results == PRINT_RESULTS) printf("%s", str);

	if(print_results == PRINT_RESULTS)
		printf("\n| cclk (Mhz)\t\t\t |");

	for (i = 0; i < sys_info.sockets; i++) {
		char temp_string[300];
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%d:CclkLimit(MHz),", i);
			append_string(&log_file_header, temp_string);
		}
		ret = esmi_cclk_limit_get(i, &cclk);
		if (!ret) {
			if(print_results == PRINT_RESULTS)
				printf(" %-17d|", cclk);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,CclkLimit(MHz)\n%d,%d\n", i, cclk);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"CclkLimit(MHz)\":%d\n\t},", i, cclk);

			if(log_to_file)
			{
				sprintf(temp_string, "%d,", cclk);
				append_string(&log_file_data, temp_string);
			}
		} else {
			err_bits |= 1 << ret;
			if(print_results == PRINT_RESULTS)
				printf(" NA (Err: %-2d)     |", ret);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,CclkLimit(MHz)\n%d,%d\n", i, cclk);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"CclkLimit(MHz)\":\"NA (Err:%d)\"\n\t},", i, ret);

			if(log_to_file)
			{
				sprintf(temp_string, "NA (Err:%d),", ret);
				append_string(&log_file_data, temp_string);
			}
		}
	}
	if (sys_info.show_addon_clock_metrics)
		sys_info.show_addon_clock_metrics(&err_bits, freq_src);

	print_socket_footer();
	if(print_results == PRINT_RESULTS) printf("\n");
	err_bits_print(err_bits);
	if (print_src)
		display_freq_limit_src_names(freq_src);
	if (err_bits > 1)
		return ESMI_MULTI_ERROR;

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_apb_enable(uint32_t sock_id)
{
	esmi_status_t ret;

	ret = esmi_apb_enable(sock_id);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to enable DF performance boost algo on "
			"socket[%d], Err[%d]: %s\n", sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("APB is enabled successfully on socket[%d]\n", sock_id);

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_set_df_pstate(uint32_t sock_id, int32_t pstate)
{
	esmi_status_t ret;

	ret = esmi_apb_disable(sock_id, pstate);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to set socket[%d] DF pstate\n", sock_id);
		printf(RED "Err[%d]: %s\n" RESET, ret, esmi_get_err_msg(ret));
		return ret;
	}

	printf("APB is disabled, P-state is set to [%d] on socket[%d] successfully\n",
		pstate, sock_id);

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_set_xgmi_width(uint8_t min, uint8_t max)
{
	esmi_status_t ret;

	ret = esmi_xgmi_width_set(min, max);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to set xGMI link width, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("xGMI link width (min:%d max:%d) is set successfully\n", min, max);

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_set_lclk_dpm_level(uint8_t sock_id, uint8_t nbio_id, uint8_t min, uint8_t max)
{
	esmi_status_t ret;

	ret = esmi_socket_lclk_dpm_level_set(sock_id, nbio_id, min, max);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to set lclk dpm level for socket[%d], nbiod[%d], Err[%d]: %s\n",
			sock_id, nbio_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("Socket[%d] nbio[%d] LCLK frequency set successfully\n",
		sock_id, nbio_id);

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_get_lclk_dpm_level(uint32_t sock_id, uint8_t nbio_id)
{
	struct dpm_level nbio;
	esmi_status_t ret;
	char temp_string[300];

	ret = esmi_socket_lclk_dpm_level_get(sock_id, nbio_id, &nbio);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get LCLK dpm level for socket[%d], nbiod[%d], "
		       "Err[%d]: %s\n",
			sock_id, nbio_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	if(log_to_file && create_log_header) {
		sprintf(temp_string, "Socket%d:NbioId%d:MinLclkDpmLevel,Socket%d:NbioId%d:MaxLclkDpmLevel,", sock_id, nbio_id, sock_id, nbio_id);
		append_string(&log_file_header, temp_string);
	}

	if(print_results == PRINT_RESULTS)
	{
		printf("\n------------------------------------\n");
		printf("| \tMIN\t | %5u\t   |\n", nbio.min_dpm_level);
		printf("| \tMAX\t | %5u\t   |\n", nbio.max_dpm_level);
		printf("------------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Socket,NbioId,MinLclkDpmLevel,MaxLclkDpmLevel\n%d,%d,%d,%d\n", sock_id, nbio_id, nbio.min_dpm_level, nbio.max_dpm_level);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"NbiodId\":%d,\n\t\t\"MinLclkDpmLevel\":%d,\n\t\t\"MaxLclkDpmLevel\":%d\n\t},", sock_id, nbio_id, nbio.min_dpm_level, nbio.max_dpm_level);

	if(log_to_file)
	{
		sprintf(temp_string, "%d,%d,", nbio.min_dpm_level, nbio.max_dpm_level);
		append_string(&log_file_data, temp_string);
	}
	return ret;
}

static int epyc_get_socketpower(void)
{
	esmi_status_t ret;
	uint32_t i;
	uint32_t power = 0, powerlimit = 0, powermax = 0;
	uint32_t err_bits = 0;
	char temp_string[300];

	print_socket_header();
	if(print_results == PRINT_RESULTS)
		printf("\n| Power (Watts)\t\t\t |");

	for (i = 0; i < sys_info.sockets; i++) {
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%d:Power(Watts),", i);
			append_string(&log_file_header, temp_string);
		}

		ret = esmi_socket_power_get(i, &power);
		if (!ret) {
			if(print_results == PRINT_RESULTS)
				printf(" %-17.3f|", (double)power/1000);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,Power(Watts)\n%d,%.3f\n", i, (double)power/1000);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"Power(Watts)\":%.3f\n\t},", i, (double)power/1000);

			if(log_to_file) {
				sprintf(temp_string, "%.3f,", (double)power/1000);
				append_string(&log_file_data, temp_string);
			}
		} else {
			err_bits |= 1 << ret;
			if(print_results == PRINT_RESULTS)
				printf(" NA (Err: %-2d)     |", ret);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,Power(Watts)\n%d,NA (Err:%d)\n", i, ret);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"Power(Watts)\":\"NA (Err:%d)\"\n\t},", i, ret);

			if(log_to_file) {
				sprintf(temp_string, "NA (Err:%d),", ret);
				append_string(&log_file_data, temp_string);
			}
		}
	}

	if(print_results == PRINT_RESULTS)
		printf("\n| PowerLimit (Watts)\t\t |");

	for (i = 0; i < sys_info.sockets; i++) {
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%d:PowerLimit(Watts),", i);
			append_string(&log_file_header, temp_string);
		}

		ret = esmi_socket_power_cap_get(i, &powerlimit);
		if (!ret) {
			if(print_results == PRINT_RESULTS)
				printf(" %-17.3f|", (double)powerlimit/1000);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,PowerLimit(Watts)\n%d,%.3f\n", i, (double)powerlimit/1000);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"PowerLimit(Watts)\":%.3f\n\t},", i, (double)powerlimit/1000);

			if(log_to_file) {
				sprintf(temp_string, "%.3f,", (double)powerlimit/1000);
				append_string(&log_file_data, temp_string);
			}
		} else {
			err_bits |= 1 << ret;
			if(print_results == PRINT_RESULTS)
				printf(" NA (Err: %-2d)     |", ret);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,PowerLimit(Watts)\n%d,NA (Err:%d)\n", i, ret);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"PowerLimit(Watts)\":\"NA (Err:%d)\"\n\t},", i, ret);

			if(log_to_file) {
				sprintf(temp_string, "NA (Err:%d),", ret);
				append_string(&log_file_data, temp_string);
			}
		}
	}

	if(print_results == PRINT_RESULTS)
		printf("\n| PowerLimitMax (Watts)\t\t |");

	for (i = 0; i < sys_info.sockets; i++) {
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%d:PowerLimitMax(Watts),", i);
			append_string(&log_file_header, temp_string);
		}
		ret = esmi_socket_power_cap_max_get(i, &powermax);
		if (!ret) {
			if(print_results == PRINT_RESULTS)
				printf(" %-17.3f|", (double)powermax/1000);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,PowerLimitMax(Watts)\n%d,%.3f\n", i, (double)powermax/1000);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"PowerLimitMax(Watts)\":%.3f\n\t},", i, (double)powermax/1000);

			if(log_to_file) {
				sprintf(temp_string, "%.3f,", (double)powermax/1000);
				append_string(&log_file_data, temp_string);
			}
		} else {
			err_bits |= 1 << ret;
			if(print_results == PRINT_RESULTS)
				printf(" NA (Err: %-2d)     |", ret);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,PowerLimitMax(Watts)\n%d,NA (Err:%d)\n", i, ret);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"PowerLimitMax(Watts)\":\"NA (Err:%d)\"\n\t},", i, ret);

			if(log_to_file) {
				sprintf(temp_string, "NA (Err:%d),", ret);
				append_string(&log_file_data, temp_string);
			}
		}
	}
	print_socket_footer();
	if(print_results == PRINT_RESULTS)
		printf("\n");
	err_bits_print(err_bits);
	if (err_bits > 1)
		return ESMI_MULTI_ERROR;
	return ESMI_SUCCESS;
}

static esmi_status_t epyc_get_coreperf(uint32_t core_id)
{
	esmi_status_t ret;
	uint32_t boostlimit = 0;
	char temp_string[300];
	/* Get the boostlimit value for a given core */
	ret = esmi_core_boostlimit_get(core_id, &boostlimit);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to get core[%d] boostlimit, Err[%d]: %s\n",
			core_id, ret, esmi_get_err_msg(ret));
		return ret;
	}

	if(log_to_file && create_log_header) {
		sprintf(temp_string, "Core%d:BoostLimit(MHz),", core_id);
		append_string(&log_file_header, temp_string);
	}

	if(print_results == PRINT_RESULTS) {
		printf("--------------------------------------------------\n");
		printf("| core[%03d] boostlimit (MHz)\t | %-10u \t |\n", core_id, boostlimit);
		printf("--------------------------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Core,BoostLimit(MHz)\n%d,%d\n", core_id, boostlimit);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"Core\":%d,\n\t\t\"BoostLimit(MHz)\":%d\n\t},", core_id, boostlimit);

	if(log_to_file)
	{
		sprintf(temp_string, "%d,", boostlimit);
		append_string(&log_file_data, temp_string);
	}

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_setpowerlimit(uint32_t sock_id, uint32_t power)
{
	esmi_status_t ret;
	uint32_t max_power = 0;

	ret = esmi_socket_power_cap_max_get(sock_id, &max_power);
	if ((ret == ESMI_SUCCESS) && (power > max_power)) {
		printf("Input power is more than max power limit,"
			" limiting to %.3f Watts\n",
			(double)max_power/1000);
		power = max_power;
	}
	ret = esmi_socket_power_cap_set(sock_id, power);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to set socket[%d] powerlimit\n", sock_id);
		printf(RED "Err[%d]: %s\n" RESET, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("Socket[%d] power_limit set to %6.03f Watts successfully\n",
		sock_id, (double)power/1000);

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_setcoreperf(uint32_t core_id, uint32_t boostlimit)
{
	esmi_status_t ret;
	uint32_t blimit = 0;

	ret = esmi_core_boostlimit_set(core_id,	boostlimit);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to set core[%d] boostlimit, Err[%d]: %s\n",
			core_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	ret = esmi_core_boostlimit_get(core_id, &blimit);
	if (ret != ESMI_SUCCESS) {
		printf("Core[%d] boostlimit set successfully, but failed to get the value\n",
			core_id);
		return ret;
	}
	if (blimit < boostlimit) {
		printf("Maximum allowed boost limit is: %u MHz\n", blimit);
		printf("Core[%d] boostlimit set to max boost limit: %u MHz\n", core_id, blimit);
	} else if (blimit > boostlimit) {
		printf("Minimum allowed boost limit is: %u MHz\n", blimit);
		printf("Core[%d] boostlimit set to min boost limit: %u MHz\n", core_id, blimit);
	} else {
		printf("Core[%d] boostlimit set to %u MHz successfully\n", core_id, blimit);
	}

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_setsocketperf(uint32_t sock_id, uint32_t boostlimit)
{
	esmi_status_t ret;
	uint32_t blimit = 0, online_core;

	ret = esmi_socket_boostlimit_set(sock_id, boostlimit);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to set socket[%d] boostlimit\n", sock_id);
		printf(RED "Err[%d]: %s\n" RESET, ret, esmi_get_err_msg(ret));
		return ret;
	}
	ret = esmi_first_online_core_on_socket(sock_id, &online_core);
	if (ret != ESMI_SUCCESS) {
		printf("Set Successful, but not verified\n");
		return ret;
	}
	ret = esmi_core_boostlimit_get(online_core, &blimit);
	if (ret != ESMI_SUCCESS) {
		printf("Socket[%d] boostlimit set successfully, but failed to get the value\n",
			sock_id);
		return ret;
	}
	if (blimit < boostlimit)
		printf("Socket[%d] boostlimit set to max boost limit: %u MHz\n", sock_id, blimit);
	else if (blimit > boostlimit)
		printf("Socket[%d] boostlimit set to min boost limit: %u MHz\n", sock_id, blimit);
	else
		printf("Socket[%d] boostlimit set to %u MHz successfully\n", sock_id, blimit);

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_get_sockc0_residency(uint32_t sock_id)
{
	esmi_status_t ret;
	uint32_t residency = 0;
	char temp_string[300];

	ret = esmi_socket_c0_residency_get(sock_id, &residency);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to get socket[%d] residency, Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}

	if(log_to_file && create_log_header) {
		sprintf(temp_string, "Socket%x:C0Residency(%%),", sock_id);
		append_string(&log_file_header, temp_string);
	}

	if(print_results == PRINT_RESULTS) {
		printf("--------------------------------------\n");
		printf("| socket[%02d] c0_residency   | %2u %%   |\n", sock_id, residency);
		printf("--------------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Socket,C0Residency(%%)\n%d,%d\n", sock_id, residency);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"C0Residency(%%)\":%d\n\t},", sock_id, residency);

	if(log_to_file) {
		sprintf(temp_string, "%x,", residency);
		append_string(&log_file_data, temp_string);
	}
	return ESMI_SUCCESS;
}

static esmi_status_t epyc_get_dimm_temp_range_refresh_rate(uint8_t sock_id, uint8_t dimm_addr)
{
	struct temp_range_refresh_rate rate;
	esmi_status_t ret;
	char temp_string[300];

	ret = esmi_dimm_temp_range_and_refresh_rate_get(sock_id, dimm_addr, &rate);
	if (ret) {
		printf("Failed to get socket[%u] DIMM temperature range and refresh rate,"
			" Err[%d]: %s\n", sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}

	if(log_to_file && create_log_header) {
		sprintf(temp_string, "Socket%d:DimmAddress0x%x:TempRange,Socket%d:DimmAddress0x%x:TempRangeRefreshRate,", sock_id, dimm_addr, sock_id, dimm_addr);
		append_string(&log_file_header, temp_string);
	}

	if(print_results == PRINT_RESULTS) {
		printf("---------------------------------------");
		printf("\n| Temp Range\t\t |");
		printf(" %-10u |", rate.range);
		printf("\n| Refresh rate\t\t |");
		printf(" %-10u |", rate.ref_rate);
		printf("\n---------------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Socket,DimmAddress,TempRange,TempRangeRefreshRate\n%d,0x%x,%u,%u\n", sock_id, dimm_addr, rate.range, rate.ref_rate);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"DimmAddress\":0x%x,\n\t\t\"TempRange\":%d,\n\t\t\"TempRangeRefreshRate\":%d\n\t},", sock_id, dimm_addr, rate.range, rate.ref_rate);

	if(log_to_file)
	{
		sprintf(temp_string, "%u,%u,", rate.range, rate.ref_rate);
		append_string(&log_file_data, temp_string);
	}

	return ret;
}

static esmi_status_t epyc_get_dimm_power(uint8_t sock_id, uint8_t dimm_addr)
{
	esmi_status_t ret;
	struct dimm_power d_power;
	char temp_string[300];

	ret = esmi_dimm_power_consumption_get(sock_id, dimm_addr, &d_power);
	if (ret) {
		printf("Failed to get socket[%u] DIMM power and update rate, Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}

	if(log_to_file && create_log_header) {
		sprintf(temp_string, "Socket%d:DimmAddress0x%x:Power(mWatts),Socket%d:DimmAddress0x%x:PowerUpdateRate(ms),", sock_id, dimm_addr, sock_id, dimm_addr);
		append_string(&log_file_header, temp_string);
	}

	if(print_results == PRINT_RESULTS) {
		printf("---------------------------------------");
		printf("\n| Power(mWatts)\t\t |");
		printf(" %-10u |", d_power.power);
		printf("\n| Power update rate(ms)\t |");
		printf(" %-10u |", d_power.update_rate);
		printf("\n| Dimm address \t\t |");
		printf(" 0x%-8x |", d_power.dimm_addr);
		printf("\n---------------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Socket,DimmAddress,Power(mWatts),PowerUpdateRate(ms)\n%d,0x%x,%u,%u\n", sock_id, d_power.dimm_addr, d_power.power, d_power.update_rate);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"DimmAddress\":0x%x,\n\t\t\"Power(mWatts)\":%d,\n\t\t\"PowerUpdateRate(ms)\":%d\n\t},", sock_id, d_power.dimm_addr, d_power.power, d_power.update_rate);

	if(log_to_file)
	{
		sprintf(temp_string, "%u,%u,", d_power.power, d_power.update_rate);
		append_string(&log_file_data, temp_string);
	}

	return ret;
}

static esmi_status_t epyc_get_dimm_thermal(uint8_t sock_id, uint8_t dimm_addr)
{
	struct dimm_thermal d_sensor;
	esmi_status_t ret;
	char temp_string[300];

	ret = esmi_dimm_thermal_sensor_get(sock_id, dimm_addr, &d_sensor);
	if (ret) {
		printf("Failed to get socket[%u] DIMM temperature and update rate,"
			" Err[%d]: %s\n", sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}

	if(log_to_file && create_log_header) {
		sprintf(temp_string, "Socket%d:DimmAddress0x%x:Temperature(°C),Socket%d:DimmAddress0x%x:TemperatureUpdateRate(ms),", sock_id, dimm_addr, sock_id, dimm_addr);
		append_string(&log_file_header, temp_string);
	}

	if(print_results == PRINT_RESULTS) {
		printf("------------------------------------------");
		printf("\n| Temperature(°C)\t |");
		printf(" %-10.3f\t |", d_sensor.temp);
		printf("\n| Update rate(ms)\t |");
		printf(" %-10u\t |", d_sensor.update_rate);
		printf("\n| Dimm address returned\t |");
		printf(" 0x%-8x\t |", d_sensor.dimm_addr);
		printf("\n------------------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Socket,DimmAddress,Temperature(°C),TemperatureUpdateRate(ms)\n%d,0x%x,%.3f,%u\n", sock_id, d_sensor.dimm_addr, d_sensor.temp, d_sensor.update_rate);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"DimmAddress\":0x%x,\n\t\t\"Temperature(°C)\":%.3f,\n\t\t\"TemperatureUpdateRate(ms)\":%d\n\t},", sock_id, d_sensor.dimm_addr, d_sensor.temp, d_sensor.update_rate);

	if(log_to_file)
	{
		sprintf(temp_string, "%.3f,%u,", d_sensor.temp, d_sensor.update_rate);
		append_string(&log_file_data, temp_string);
	}

	return ret;
}

static esmi_status_t epyc_get_curr_freq_limit_core(uint32_t core_id)
{
	esmi_status_t ret;
	uint32_t cclk;

	ret = esmi_current_freq_limit_core_get(core_id, &cclk);
	if (ret) {
		printf("Failed to get current clock frequency limit for core[%3d], Err[%d]: %s\n",
			core_id, ret, esmi_get_err_msg(ret));
		return ret;
	}

	if(print_results == PRINT_RESULTS) {
		printf("--------------------------------------------------------------");
		printf("\n| CPU[%03d] core clock current frequency limit (MHz) : %u\t|\n", core_id, cclk);
		printf("--------------------------------------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Core,CoreFrequencyLimit(MHz)\n%d,%u\n", core_id, cclk);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"Core\":%d,\n\t\t\"CoreFrequencyLimit(MHz)\":%u\n\t},", core_id, cclk);

	if(log_to_file) {
		char temp_string[300];
		if(create_log_header) {
			sprintf(temp_string, "Core%d:CoreFrequencyLimit(MHz),", core_id);
			append_string(&log_file_header, temp_string);
		}
		sprintf(temp_string, "%u,", cclk);
		append_string(&log_file_data, temp_string);
	}
	return ret;
}

static int epyc_get_power_telemetry()
{
	esmi_status_t ret;
	uint32_t power;
	int i;
	uint32_t err_bits = 0;

	print_socket_header();
	if(print_results == PRINT_RESULTS)
		printf("\n| SVI Power Telemetry (Watts) \t |");

	for (i = 0; i < sys_info.sockets; i++) {
		char temp_string[300];
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%d:SVIPowerTelemetry(Watts),", i);
			append_string(&log_file_header, temp_string);
		}

		ret = esmi_pwr_svi_telemetry_all_rails_get(i, &power);
		if(!ret) {
			if(print_results == PRINT_RESULTS)
				printf(" %-17.3f|", (double)power/1000);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,SVIPowerTelemetry(Watts)\n%d,%.3f\n", i, (double)power/1000);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"SVIPowerTelemetry(Watts)\":%.3f\n\t},", i, (double)power/1000);

			if(log_to_file) {
				sprintf(temp_string, "%.3f,", (double)power/1000);
				append_string(&log_file_data, temp_string);
			}
		} else {
			err_bits |= 1 << ret;
			if(print_results == PRINT_RESULTS)
				printf(" NA (Err: %-2d)     |", ret);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,SVIPowerTelemetry(Watts)\n%d,NA (Err:%d)\n", i, ret);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"SVIPowerTelemetry(Watts)\":\"NA (Err:%d)\"\n\t},", i, ret);

			if(log_to_file) {
				sprintf(temp_string, "NA (Err:%d),", ret);
				append_string(&log_file_data, temp_string);
			}
		}
	}

	print_socket_footer();
	err_bits_print(err_bits);
	if (err_bits > 1)
		return ESMI_MULTI_ERROR;

	return ESMI_SUCCESS;
}

/* order here should match with bw_string[] in esmi.h */
const char *bw_type_list[3] = {"AGG_BW", "RD_BW", "WR_BW"};

static void find_bwtype_index(char *bw_type, int *bw_type_ind)
{
	int i;

	if (bw_type == NULL)
		return;
	for(i = 0; i < ARRAY_SIZE(bw_type_list); i++) {
		if(!strcmp(bw_type, bw_type_list[i])) {
			*bw_type_ind = i;
			break;
		}
	}

}

static esmi_status_t epyc_get_io_bandwidth_info(uint32_t sock_id, char *link)
{
	esmi_status_t ret;
	uint32_t bw;
	struct link_id_bw_type io_link;
	char temp_string[300];

	io_link.link_name = link;
	/* Aggregate bw = 1 */
	io_link.bw_type = 1 ;
	ret = esmi_current_io_bandwidth_get(sock_id, io_link, &bw);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get io bandwidth width for socket[%u] Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}

	if(log_to_file && create_log_header) {
		sprintf(temp_string, "Socket%d:Link%s:CurrentIOAggregateBandwidth(Mbps),", sock_id, link);
		append_string(&log_file_header, temp_string);
	}

	if(print_results == PRINT_RESULTS) {
		printf("\n-----------------------------------------------------------\n");
		printf("| Current IO Aggregate bandwidth of link %s | %6u Mbps |\n", link, bw);
		printf("-----------------------------------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Socket,Link,CurrentAggregateIOBandwidth(Mbps)\n%d,%s,%u\n", sock_id, link, bw);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"Link\":%s,\n\t\t\"CurrentIOAggregateBandwidth(Mbps)\":%u\n\t},", sock_id, link, bw);

	if(log_to_file)
	{
		sprintf(temp_string, "%u,", bw);
		append_string(&log_file_data, temp_string);
	}

	return ret;
}

static esmi_status_t epyc_get_xgmi_bandwidth_info(uint32_t sock_id, char *link, char *bw_type)
{
	struct link_id_bw_type xgmi_link;
	esmi_status_t ret;
	uint32_t bw;
	int bw_ind = -1;
	char temp_string[300];

	find_bwtype_index(bw_type, &bw_ind);
	if (bw_ind == -1) {
		printf("Please provide valid link bandwidth type.\n");
		printf(MAG "Try --help for more information.\n" RESET);
		return ESMI_INVALID_INPUT;
	}

	xgmi_link.link_name = link;
	xgmi_link.bw_type = 1 << bw_ind;
	ret = esmi_current_xgmi_bw_get(sock_id, xgmi_link, &bw);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get xgmi bandwidth width, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}

	if(log_to_file && create_log_header) {
		sprintf(temp_string, "Socket%d:Link%s:Bw%s:XgmiBandwidth(Mbps),", sock_id, link, bw_type);
		append_string(&log_file_header, temp_string);
	}

	if(print_results == PRINT_RESULTS) {
		printf("\n-------------------------------------------------------------\n");
		printf("| Current %s bandwidth of xGMI link %s | %6u Mbps |\n", bw_string[bw_ind], link, bw);
		printf("-------------------------------------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Socket,Link,Bw,XgmiBandwidth(Mbps)\n%d,%s,%s,%u\n", sock_id, link, bw_type, bw);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"Link\":\"%s\",\n\t\t\"Bw\":\"%s\",\n\t\t\"XgmiBandwidth(Mbps)\":%u\n\t},", sock_id, link, bw_type, bw);

	if(log_to_file)
	{
		sprintf(temp_string, "%u,", bw);
		append_string(&log_file_data, temp_string);
	}

	return ret;
}

static char *pcie_strings[] = {
	"automatically detect based on bandwidth utilisation",
	"limited to Gen4 rate",
	"limited to Gen5 rate"
};

static esmi_status_t epyc_set_pciegen5_rate_ctl(uint8_t sock_id, uint8_t rate_ctrl)
{
	uint8_t prev_mode;
	esmi_status_t ret;

	ret = esmi_pcie_link_rate_set(sock_id, rate_ctrl, &prev_mode);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to set pcie link rate control for socket[%u], Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}

	printf("Pcie link rate is set to %u (i.e. %s) successfully.\n",
	       rate_ctrl, pcie_strings[rate_ctrl]);
	printf("\nPrevious pcie link rate control was : %u\n", prev_mode);

	return ret;
}

static esmi_status_t epyc_set_power_efficiency_mode(uint32_t sock_id, uint8_t mode)
{
	esmi_status_t ret;

	ret = esmi_pwr_efficiency_mode_set(sock_id, mode);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to set power efficiency mode for socket[%u], Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("Power efficiency profile policy is set to %d successfully\n", mode);

	return ret;
}

static esmi_status_t epyc_set_df_pstate_range(uint8_t sock_id, uint8_t max_pstate, uint8_t min_pstate)
{
	esmi_status_t ret;

	ret = esmi_df_pstate_range_set(sock_id, max_pstate, min_pstate);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to set df pstate range, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("Data Fabric PState range(max:%d min:%d) set successfully\n",
		max_pstate, min_pstate);
	return ret;
}

static esmi_status_t epyc_set_gmi3_link_width(uint8_t sock_id, uint8_t min, uint8_t max)
{
	esmi_status_t ret;

	ret = esmi_gmi3_link_width_range_set(sock_id, min, max);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to set gmi3 link width for socket[%u] Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("Gmi3 link width range is set successfully\n");

	return ret;
}

static esmi_status_t epyc_get_curr_freq_limit_socket(uint32_t sock_id)
{
	esmi_status_t ret;
	uint32_t cclk;

	ret = esmi_cclk_limit_get(sock_id, &cclk);
	if (ret) {
		printf("Failed to get current clock frequency limit for socket[%d], Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}

	if(print_results == PRINT_RESULTS) {
		printf("----------------------------------------------------------------");
		printf("\n| SOCKET[%d] core clock current frequency limit (MHz) : %u\t|\n", sock_id, cclk);
		printf("----------------------------------------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Socket,SocketCclkFrequencyLimit(MHz)\n%d,%u\n", sock_id, cclk);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"SocketCclkFrequencyLimit(MHz)\":%u\n\t},", sock_id, cclk);

	if(log_to_file) {
		char temp_string[300];
		if(create_log_header) {
			sprintf(temp_string, "Socket%d:SocketCclkFrequencyLimit(MHz),", sock_id);
			append_string(&log_file_header, temp_string);
		}
		sprintf(temp_string, "%u,", cclk);
		append_string(&log_file_data, temp_string);
	}

	return ret;
}

static void show_smi_message(void)
{
	if(print_results == PRINT_RESULTS) 		printf("\n============================= E-SMI ===================================\n\n");
	else if(print_results == PRINT_RESULTS_AS_JSON) printf("{");
}

static void show_smi_end_message(void)
{
	if(print_results == PRINT_RESULTS)		printf("\n============================= End of E-SMI ============================\n");
	else if(print_results == PRINT_RESULTS_AS_JSON)
	{
		printf("\n\t{\n\t\t\"JSONFormatVersion\":1\n\t}");
		printf("\n}\n");
	}
}

static void no_addon_socket_metrics(uint32_t *err_bits, char **freq_src)
{
	return;
}

static void no_addon_cpu_metrics(uint32_t *err_bits)
{
	return;
}

static void no_addon_clock_metrics(uint32_t *err_bits, char **freq_src)
{
	return;
}

static void clock_ver5_metrics(uint32_t *err_bits, char **freq_src)
{
	get_sock_freq_limit(err_bits, freq_src);
	get_sock_freq_range(err_bits);
}

static void socket_ver4_metrics(uint32_t *err_bits, char **freq_src)
{
	esmi_status_t ret;
	uint32_t tmon;
	int i;

	ddr_bw_get(err_bits);
	printf("\n| Temperature (°C)\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		ret = esmi_socket_temperature_get(i, &tmon);
		if (!ret) {
			printf(" %-17.3f|", (double)tmon/1000);
		} else {
			*err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}
}

static void socket_ver6_metrics(uint32_t *err_bits, char **freq_src)
{
	get_sock_freq_limit(err_bits, freq_src);
	get_sock_freq_range(err_bits);
}

static void socket_ver5_metrics(uint32_t *err_bits, char **freq_src)
{
	ddr_bw_get(err_bits);
	get_sock_freq_limit(err_bits, freq_src);
	get_sock_freq_range(err_bits);
}

static int show_socket_metrics(uint32_t *err_bits, char **freq_src)
{
	esmi_status_t ret;
	uint32_t i;
	uint64_t pkg_input = 0;
	uint32_t power = 0, powerlimit = 0, powermax = 0;
	uint32_t c0resi;
	char temp_string[300];

	print_socket_header();
	if(print_results == PRINT_RESULTS)
		printf("\n| Energy (K Joules)\t\t |");

	for (i = 0; i < sys_info.sockets; i++) {
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%d:SocketEnergy(KJoules),", i);
			append_string(&log_file_header, temp_string);
		}

		ret = esmi_socket_energy_get(i, &pkg_input);
		if (!ret) {
			if(print_results == PRINT_RESULTS)
				printf(" %-17.3lf|", (double)pkg_input/1000000000);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,SocketEnergy(KJoules)\n%d,%.3f\n", i, (double)pkg_input/1000000000);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"SocketEnergy(KJoules)\":%.3f\n\t},", i, (double)pkg_input/1000000000);

			if(log_to_file) {
				sprintf(temp_string, "%.3f,", (double)pkg_input/1000000000);
				append_string(&log_file_data, temp_string);
			}
		} else {
			*err_bits |= 1 << ret;
			if(print_results == PRINT_RESULTS)
				printf(" NA (Err: %-2d)     |", ret);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,SocketEnergy(KJoules)\n%d,NA (Err:%d)\n", i, ret);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"SocketEnergy(KJoules)\":\"NA (Err:%d)\"\n\t},", i, ret);

			if(log_to_file) {
				sprintf(temp_string, "NA (Err:%d),", ret);
				append_string(&log_file_data, temp_string);
			}
		}
	}

	if(print_results == PRINT_RESULTS)
		printf("\n| Power (Watts)\t\t\t |");

	for (i = 0; i < sys_info.sockets; i++) {
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%d:SocketPower(Watts),", i);
			append_string(&log_file_header, temp_string);
		}

		ret = esmi_socket_power_get(i, &power);
		if (!ret) {

			if(print_results == PRINT_RESULTS)
				printf(" %-17.3f|", (double)power/1000);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,SocketPower(Watts)\n%d,%.3f\n", i, (double)power/1000);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"SocketPower(Watts)\":%.3f\n\t},", i, (double)power/1000);

			if(log_to_file) {
				sprintf(temp_string, "%.3f,", (double)power/1000);
				append_string(&log_file_data, temp_string);
			}
		} else {
			*err_bits |= 1 << ret;
			if(print_results == PRINT_RESULTS)
				printf(" NA (Err: %-2d)     |", ret);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,SocketPower(Watts)\n%d,NA (Err:%d)\n", i, ret);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"SocketPower(Watts)\":\"NA (Err:%d)\"\n\t},", i, ret);

			if(log_to_file) {
				sprintf(temp_string, "NA (Err:%d),", ret);
				append_string(&log_file_data, temp_string);
			}
		}
	}


	if(print_results == PRINT_RESULTS)
		printf("\n| PowerLimit (Watts)\t\t |");

	for (i = 0; i < sys_info.sockets; i++) {
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%d:SocketPowerLimit(Watts),", i);
			append_string(&log_file_header, temp_string);
		}

		ret = esmi_socket_power_cap_get(i, &powerlimit);
		if (!ret) {
			if(print_results == PRINT_RESULTS)
				printf(" %-17.3f|", (double)powerlimit/1000);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,SocketPowerLimit(Watts)\n%d,%.3f\n", i, (double)powerlimit/1000);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"SocketPowerLimit(Watts)\":%.3f\n\t},", i, (double)powerlimit/1000);

			if(log_to_file) {
				sprintf(temp_string, "%.3f,", (double)powerlimit/1000);
				append_string(&log_file_data, temp_string);
			}
		} else {
			*err_bits |= 1 << ret;
			if(print_results == PRINT_RESULTS)
				printf(" NA (Err: %-2d)     |", ret);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,SocketPowerLimit(Watts)\n%d,NA (Err:%d)\n", i, ret);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"SocketPowerLimit(Watts)\":\"NA (Err:%d)\"\n\t},", i, ret);

			if(log_to_file) {
				sprintf(temp_string, "NA (Err:%d),", ret);
				append_string(&log_file_data, temp_string);
			}
		}
	}

	if(print_results == PRINT_RESULTS)
		printf("\n| PowerLimitMax (Watts)\t\t |");

	for (i = 0; i < sys_info.sockets; i++) {
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%d:SocketPowerLimitiMax(Watts),", i);
			append_string(&log_file_header, temp_string);
		}

		ret = esmi_socket_power_cap_max_get(i, &powermax);
		if(!ret) {
			if(print_results == PRINT_RESULTS)
				printf(" %-17.3f|", (double)powermax/1000);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,SocketPowerLimitMax(Watts)\n%d,%.3f\n", i, (double)powermax/1000);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"SocketPowerLimitMax(Watts)\":%.3f\n\t},", i, (double)powermax/1000);

			if(log_to_file) {
				sprintf(temp_string, "%.3f,", (double)powermax/1000);
				append_string(&log_file_data, temp_string);
			}
		} else {
			*err_bits |= 1 << ret;
			if(print_results == PRINT_RESULTS)
				printf(" NA (Err: %-2d)     |", ret);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,SocketPowerLimitMax(Watts)\n%d,NA (Err:%d)\n", i, ret);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"SocketPowerLimitMax(Watts)\":\"NA (Err:%d)\"\n\t},", i, ret);

			if(log_to_file) {
				sprintf(temp_string, "NA (Err:%d),", ret);
				append_string(&log_file_data, temp_string);
			}
		}
	}

	if(print_results == PRINT_RESULTS)
		printf("\n| C0 Residency (%%)\t\t |");

	for (i = 0; i < sys_info.sockets; i++) {
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Socket%d:SocketC0Residency(%%),", i);
			append_string(&log_file_header, temp_string);
		}
		ret = esmi_socket_c0_residency_get(i, &c0resi);
		if(!ret) {
			if(print_results == PRINT_RESULTS)
				printf(" %-17u|", c0resi);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,SocketC0Residency(%%)\n%d,%u\n", i, c0resi);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"SocketC0Residency(%%)\":%u\n\t},", i, c0resi);

			if(log_to_file) {
				sprintf(temp_string, "%u,", c0resi);
				append_string(&log_file_data, temp_string);
			}
		} else {
			*err_bits |= 1 << ret;
			if(print_results == PRINT_RESULTS)
				printf(" NA (Err: %-2d)     |", ret);
			else if(print_results == PRINT_RESULTS_AS_CSV)
				printf("Socket,SocketC0Residency(%%)\n%d,NA (Err:%d)\n", i, ret);
			else if(print_results == PRINT_RESULTS_AS_JSON)
				printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"SocketC0Residency(%%)\":\"NA (Err:%d)\"\n\t},", i, ret);

			if(log_to_file) {
				sprintf(temp_string, "NA (Err:%d),", ret);
				append_string(&log_file_data, temp_string);
			}
		}
	}
	/* proto version specific socket metrics are printed here */
	if (sys_info.show_addon_socket_metrics)
		sys_info.show_addon_socket_metrics(err_bits, freq_src);

	print_socket_footer();
	if (*err_bits > 1)
		return ESMI_MULTI_ERROR;
	return ESMI_SUCCESS;
}

static esmi_status_t cache_system_info(void)
{
	esmi_status_t ret;

	ret = esmi_number_of_cpus_get(&sys_info.cpus);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get number of cpus, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	ret = esmi_number_of_sockets_get(&sys_info.sockets);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get number of sockets, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	ret = esmi_threads_per_core_get(&sys_info.threads_per_core);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get threads per core, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	ret = esmi_cpu_family_get(&sys_info.family);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get cpu family, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	ret = esmi_cpu_model_get(&sys_info.model);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get cpu model, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}

	return ESMI_SUCCESS;
}

static void show_system_info(void)
{
	if(print_results == PRINT_RESULTS)
	{
		printf("--------------------------------------\n");
		printf("| CPU Family		| 0x%-2x (%-3d) |\n", sys_info.family, sys_info.family);
		printf("| CPU Model		| 0x%-2x (%-3d) |\n", sys_info.model, sys_info.model);
		printf("| NR_CPUS		| %-8d   |\n", sys_info.cpus);
		printf("| NR_SOCKETS		| %-8d   |\n", sys_info.sockets);
		if (sys_info.threads_per_core > 1) {
			printf("| THREADS PER CORE	| %d (SMT ON) |\n", sys_info.threads_per_core);
		} else {
			printf("| THREADS PER CORE	| %d (SMT OFF)|\n", sys_info.threads_per_core);
		}
		printf("--------------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("CpuFamily(Hex),CpuModel(Hex),NumberOfCpus,NumberOfSockets,ThreadsPerCore\n0x%x,0x%x,%d,%d,%d\n", sys_info.family, sys_info.model, sys_info.cpus, sys_info.sockets, sys_info.threads_per_core);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"CpuFamily(Hex)\":\"0x%x\",\n\t\t\"CpuModel(Hex)\":\"0x%x\",\n\t\t\"NumberOfCpus\":%d,\n\t\t\"NumberOfSockets\":%d,\n\t\t\"ThreadsPerCore\":%x\n\t},", sys_info.family, sys_info.model, sys_info.cpus, sys_info.sockets, sys_info.threads_per_core);
}

static esmi_status_t show_cpu_energy_all(void)
{
	int i;
	uint64_t *input;
	uint32_t cpus;
	esmi_status_t ret;
	char temp_string[300];

	cpus = sys_info.cpus/sys_info.threads_per_core;

	input = (uint64_t *) calloc(cpus, sizeof(uint64_t));
	if (NULL == input) {
		printf("Memory allocation failed all energy entries\n");
		return ESMI_NO_MEMORY;
	}

	ret = esmi_all_energies_get(input);
	if (ret != ESMI_SUCCESS) {
		printf("\nFailed: to get CPU energies, Err[%d]: %s",
			ret, esmi_get_err_msg(ret));
		free(input);
		return ret;
	}
	if(print_results == PRINT_RESULTS)
		printf("\n| CPU energies in Joules:\t\t\t\t\t\t\t\t\t\t\t|");
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Core,CoreEnergy(Joules)\n");

	for (i = 0; i < cpus; i++) {
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Core%d:CoreEnergy(Joules),", i);
			append_string(&log_file_header, temp_string);
		}

		if(print_results == PRINT_RESULTS) {
			if(!(i % 8)) {
				printf("\n| cpu [%3d] :", i);
			}
			printf(" %10.3lf", (double)input[i]/1000000);
			if (i % 8 == 7)
				printf("\t\t|");
		}
		else if(print_results == PRINT_RESULTS_AS_CSV)
			printf("%d,%.3f\n", i, (double)input[i]/1000000);
		else if(print_results == PRINT_RESULTS_AS_JSON)
			printf("\n\t{\n\t\t\"Core\":%d,\n\t\t\"CoreEnergy(Joules)\":%.3f\n\t},", i, (double)input[i]/1000000);

		if(log_to_file) {
			sprintf(temp_string, "%.3f,", (double)input[i]/1000000);
			append_string(&log_file_data, temp_string);
		}
	}
	free(input);

	return ESMI_SUCCESS;
}

static esmi_status_t show_cpu_boostlimit_all(void)
{
	int i;
	uint32_t boostlimit;
	uint32_t cpus;
	esmi_status_t ret;
	char temp_string[300];

	cpus = sys_info.cpus/sys_info.threads_per_core;

	if(print_results == PRINT_RESULTS)
		printf("\n| CPU boostlimit in MHz:\t\t\t\t\t\t\t\t\t\t\t|");

	for (i = 0; i < cpus; i++) {
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Core%d:CoreBoostLimit(MHz),", i);
			append_string(&log_file_header, temp_string);
		}

		boostlimit = 0;
		ret = esmi_core_boostlimit_get(i, &boostlimit);
		if(!ret) {
			sprintf(temp_string, "%d", boostlimit);
		} else {
			sprintf(temp_string, "NA");
		}

		if(print_results == PRINT_RESULTS)
		{
			if(!(i % 16)) {
				printf("\n| cpu [%3d] :", i);
			}
			if (!ret) {
				printf(" %-5u", boostlimit);
			} else {
				printf(" NA   ");
			}
			if (i % 16 == 15)
				printf("   |");
		}
		else if(print_results == PRINT_RESULTS_AS_CSV)
			printf("Core,CoreBoostLimit(MHz)\n%d,%s\n", i, temp_string);
		else if(print_results == PRINT_RESULTS_AS_JSON)
			printf("\n\t{\n\t\t\"Core\":%d,\n\t\t\"CoreBoostLimit(MHz)\":%s\n\t},", i, temp_string);

		if(log_to_file) {
			char temp_string_1[300+1];
			sprintf(temp_string_1, "%s,", temp_string);
			append_string(&log_file_data, temp_string_1);
		}
	}

	return ESMI_SUCCESS;
}

static esmi_status_t show_core_clocks_all()
{
	esmi_status_t ret;
	uint32_t cpus;
	uint32_t cclk;
	int i;
	char temp_string[300];

	cpus = sys_info.cpus/sys_info.threads_per_core;

	if(print_results == PRINT_RESULTS)
		printf("\n| CPU core clock current frequency limit in MHz:\t\t\t\t\t\t\t\t\t\t\t|");

	for (i = 0; i < cpus; i++) {
		if(log_to_file && create_log_header) {
			sprintf(temp_string, "Core%d:CoreCclkFrequencyLimit(MHz),", i);
			append_string(&log_file_header, temp_string);
		}
	}

	for (i = 0; i < cpus; i++) {
		cclk = 0;
		ret = esmi_current_freq_limit_core_get(i, &cclk);
		if(!ret) {
			sprintf(temp_string, "%d", cclk);
		} else {
			sprintf(temp_string, "NA");
		}
		if(print_results == PRINT_RESULTS) {
			if(!(i % 16)) {
				printf("\n| cpu [%3d] :", i);
			}
			if (!ret) {
				printf(" %-5u", cclk);
			} else {
				printf(" NA   ");
			}
			if (i % 16 == 15)
				printf("   |");
		}
		else if(print_results == PRINT_RESULTS_AS_CSV)
			printf("Core,CoreCclkFrequencyLimit(MHz)\n%d,%s\n", i, temp_string);
		else if(print_results == PRINT_RESULTS_AS_JSON)
			printf("\n\t{\n\t\t\"Core\":%d,\n\t\t\"CoreCclkFrequencyLimit(MHz)\":%s\n\t},", i, temp_string);

		if(log_to_file) {
			char temp_string_1[300+1];
			sprintf(temp_string_1, "%s,", temp_string);
			append_string(&log_file_data, temp_string_1);
		}
	}
	return ESMI_SUCCESS;
}

static void cpu_ver5_metrics(uint32_t *err_bits)
{
	esmi_status_t ret;

	if(print_results == PRINT_RESULTS)
		printf("\n--------------------------------------------------------------------"
			"---------------------------------------------");
	ret = show_core_clocks_all();
	*err_bits |= 1 << ret;

	if(print_results == PRINT_RESULTS)
		printf("\n--------------------------------------------------------------------"
			"---------------------------------------------");
}

static int show_cpu_metrics(uint32_t *err_bits)
{
	esmi_status_t ret;

	if(print_results == PRINT_RESULTS)
		printf("\n\n--------------------------------------------------------------------"
			"---------------------------------------------");
	ret = show_cpu_energy_all();
	*err_bits |= 1 << ret;

	if(print_results == PRINT_RESULTS) {
		printf("\n--------------------------------------------------------------------"
			"---------------------------------------------\n");

		printf("\n--------------------------------------------------------------------"
			"---------------------------------------------");
	}
	ret = show_cpu_boostlimit_all();
	*err_bits |= 1 << ret;

	if(print_results == PRINT_RESULTS)
		printf("\n--------------------------------------------------------------------"
			"---------------------------------------------\n");

	/* proto version specific cpu metrics are printed here */
	if (sys_info.show_addon_cpu_metrics)
		sys_info.show_addon_cpu_metrics(err_bits);

	if (*err_bits > 1)
		return ESMI_MULTI_ERROR;

	return ESMI_SUCCESS;
}

static int test_hsmp_mailbox(uint8_t sock_id, uint32_t input_data)
{
	uint32_t data = input_data;
	esmi_status_t ret;

	ret = esmi_test_hsmp_mailbox(sock_id, &data);
	if (ret) {
		printf("Failed to test hsmp mailbox on socket[%u], Err[%d] : %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	if (data != (input_data + 1)) {
		printf("------------------------------------");
		printf("\n| Socket[%u] Test message FAILED | \n", sock_id);
		printf("| Expected : %-5u hex: %#-5x | ", (input_data + 1), (input_data + 1));
		printf("\n| Received : %-5u hex: %#-5x |", data, data);
		printf("\n------------------------------------------\n");
		return 0;
	}
	printf("------------------------------------------");
	printf("\n| Socket[%u] Test message PASSED | \n", sock_id);
	printf("| Expected : %-5u hex: %#-5x | ", (input_data + 1), (input_data + 1));
	printf("\n| Received : %-5u hex: %#-5x |", data, data);
	printf("\n------------------------------------------\n");
	return 0;
}

static int show_smi_all_parameters(void)
{
	char *freq_src[ARRAY_SIZE(freqlimitsrcnames) * sys_info.sockets];
	int i;
	uint32_t err_bits = 0;

	for (i = 0; i < (ARRAY_SIZE(freqlimitsrcnames) * sys_info.sockets); i++)
		freq_src[i] = NULL;

	show_system_info();

	show_socket_metrics(&err_bits, freq_src);

	show_cpu_metrics(&err_bits);

	if(print_results == PRINT_RESULTS)
		printf("\n");
	if (print_src)
		display_freq_limit_src_names(freq_src);
	err_bits_print(err_bits);
	if (err_bits > 1)
		return ESMI_MULTI_ERROR;

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_get_metrics_table_version(void)
{
	uint32_t met_ver;
	esmi_status_t ret;

	ret = esmi_metrics_table_version_get(&met_ver);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get Metrics Table Version, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("\n------------------------------------------");
	printf("\n| METRICS TABLE Version   |  %u \t\t |\n", met_ver);
	printf("------------------------------------------\n");

	return ESMI_SUCCESS;
}

uint32_t check_msb_32(uint32_t num)
{
	uint32_t msb;
	msb = 1UL << (NUM_OF_32BITS - 1);
	/*If msb = 1 , then take 2'complement of the number*/
	if(num & msb) {
		num = ~num + 1;
		return num;
	}
	else
		return num;
}

uint64_t check_msb_64(uint64_t num)
{
	uint64_t msb;
	msb = 1UL << (NUM_OF_64BITS - 1);
	/*If msb = 1 , then take 2'complement of the number*/
	if(num & msb) {
		num = ~num + 1;
		return num;
	}
	else
		return num;
}


static esmi_status_t epyc_show_metrics_table(uint8_t sock_id)
{
	esmi_status_t ret;
	struct hsmp_metric_table mtbl = {0};
	time_t rawtime;
	struct tm *timeinfo;
	int i, j, x;
	uint32_t num1 = 0;
	uint64_t num2 = 0;

	double fraction_q10 = 1/pow(2,10);
	double fraction_uq10 = fraction_q10;
	double fraction_uq16 = 1/pow(2,16) ;

	ret = esmi_metrics_table_get(sock_id, &mtbl);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get Metrics Table for socket [%d], Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}

	printf("-------------------------------------------------------------------------");
	printf("\n| \t\t\tMETRICS TABLE (FAMILY:0x%x,MODEL:0x%x)    \t|", sys_info.family, sys_info.model);
	printf("\n-------------------------------------------------------------------------");
	printf("\n\n-------------------------------------------------------------------------");
	printf("\n| ACCUMULATOR COUNTER                   |  %-20u\t\t|\n", mtbl.accumulation_counter);
	printf("-------------------------------------------------------------------------");
	printf("\n\n");
	printf("-------------------------------------------------------------------------");
	num1 = check_msb_32(mtbl.max_socket_temperature);
	printf("\n| MAX SOCKET TEMP                       |  %18.3lf °C\t| ",
			(num1 * fraction_q10));
	num1 = check_msb_32(mtbl.max_vr_temperature);
	printf("\n| MAX VR TEMP                           |  %18.3lf °C\t| ",
			(num1 * fraction_q10));
	num1 = check_msb_32(mtbl.max_hbm_temperature);
	printf("\n| MAX HBM TEMP                          |  %18.3lf °C\t| ",
			(num1 * fraction_q10));
	num2 = check_msb_64(mtbl.max_socket_temperature_acc);
	printf("\n| MAX SOCKET TEMP ACC                   |  %18.3lf °C\t| ",
			(num2 * fraction_q10));
	num2 = check_msb_64(mtbl.max_vr_temperature_acc);
	printf("\n| MAX VR TEMP ACC                       |  %18.3lf °C\t| ",
			(num2 * fraction_q10));
	num2 = check_msb_64(mtbl.max_hbm_temperature_acc);
	printf("\n| MAX HBM TEMP ACC                      |  %18.3lf °C\t| \n",
			(num2 * fraction_q10));
	printf("-------------------------------------------------------------------------");
	printf("\n");
	printf("\n-----------------------------------------------------------------");
	printf("\n| SOCKET POWER LIMIT                    |  %5.3lf W\t\t| ",
			(mtbl.socket_power_limit * fraction_uq10));
	printf("\n| MAX SOCKET POWER LIMIT                |  %5.3lf W\t\t| ",
			(mtbl.max_socket_power_limit * fraction_uq10));
	printf("\n| SOCKET POWER                          |  %5.3lf W\t\t| \n",
			(mtbl.socket_power * fraction_uq10));

	printf("-----------------------------------------------------------------");
	printf("\n");
	printf("\n-------------------------------------------------------------------------");
	rawtime = (time_t)mtbl.timestamp;
	time (&rawtime);
	timeinfo = localtime (&rawtime);
	printf("\n| TIMESTAMP Raw                         |  %20llu\t\t|", mtbl.timestamp);
	printf("\n| TIMESTAMP Readable                    |  %s\t\t", asctime(timeinfo));

	printf("\n| SOCKET ENERGY ACC                     |  %15.3lf kJ\t\t| ",
			(mtbl.socket_energy_acc * fraction_uq16)/KILO);
	printf("\n| CCD ENERGY ACC                        |  %15.3lf kJ\t\t| ",
			(mtbl.ccd_energy_acc * fraction_uq16)/KILO);
	printf("\n| XCD ENERGY ACC                        |  %15.3lf kJ\t\t| ",
			(mtbl.xcd_energy_acc * fraction_uq16)/KILO);
	printf("\n| AID ENERGY ACC                        |  %15.3lf kJ\t\t| ",
			(mtbl.aid_energy_acc * fraction_uq16)/KILO);
	printf("\n| HBM ENERGY ACC                        |  %15.3lf kJ\t\t| \n",
			(mtbl.hbm_energy_acc * fraction_uq16)/KILO);
	printf("-------------------------------------------------------------------------");
	printf("\n");
	printf("\n-----------------------------------------------------------------");
	printf("\n| CCLK frequency limit                  |  %5.3lf GHz\t\t| ",
			(mtbl.cclk_frequency_limit * fraction_uq10));
	printf("\n| GFXCLK frequency limit                |  %5.3lf MHz\t\t| ",
			(mtbl.gfxclk_frequency_limit * fraction_uq10));
	printf("\n| Effective FCLK frequency              |  %5.3lf MHz\t\t| ",
			(mtbl.fclk_frequency * fraction_uq10));
	printf("\n| Effective UCLK frequency              |  %5.3lf MHz\t\t| \n",
			(mtbl.uclk_frequency * fraction_uq10));

	printf("-----------------------------------------------------------------");
	printf("\n\n-------------------------------------------------------------------------");
	printf("\n| Effective frequency per AID: \t\t\t\t\t\t|");
	printf("\n-------------------------------------------------------------------------");
	printf("\n| AID | SOCCLK \t\t| VCLK \t\t| DCLK \t\t| LCLK \t\t|");
	printf("\n-------------------------------------------------------------------------");
	for(i = 0; i < AID_COUNT ; i++){
		printf("\n| [%d] | %5.3lf MHz\t| %5.3lf MHz\t| %5.3lf MHz\t| %5.3lf MHz\t| ",
				i, (mtbl.socclk_frequency[i] * fraction_uq10),
				(mtbl.vclk_frequency[i] * fraction_uq10),
				(mtbl.dclk_frequency[i] * fraction_uq10),
				(mtbl.lclk_frequency[i] * fraction_uq10));
	}
	printf("\n-------------------------------------------------------------------------\n");
	printf("\n---------------------------------------------------------------------------------------------------------");
	printf("\n| List of supported frequencies(0 means state is not supported):\t\t\t\t\t|");
	printf("\n---------------------------------------------------------------------------------------------------------");
	printf("\n| AID | FCLK \t\t| UCLK \t\t| SOCCLK \t| VCLK \t\t| DCLK \t\t| LCLK \t\t|");
	printf("\n---------------------------------------------------------------------------------------------------------");
	for(i = 0; i < AID_COUNT ; i++){
		printf("\n| [%d] |%5.3lf MHz\t|%5.3lf MHz\t|%5.3lf MHz\t|%5.3lf MHz\t|%5.3lf MHz\t|%5.3lf MHz\t|",
				i, (mtbl.fclk_frequency_table[i] * fraction_uq10),
				(mtbl.uclk_frequency_table[i] * fraction_uq10),
				(mtbl.socclk_frequency_table[i] * fraction_uq10),
				(mtbl.vclk_frequency_table[i] * fraction_uq10),
				(mtbl.dclk_frequency_table[i] * fraction_uq10),
				(mtbl.lclk_frequency_table[i] * fraction_uq10));
	}
	printf("\n--------------------------------------------------------------------------------------------------------\n");

	uint32_t cpus = sys_info.cpus/sys_info.threads_per_core;
	printf("\n---------------------------------------------------------------------------------------------------");
	printf("---------------");
	printf("\n| CCLK frequency accumulated for target CPUs:\t\t\t\t\t\t\t\t\t |");
	printf("\n--------------------------------------------------------------------------------------------------");
	printf("---------------");
	printf("\n");
	x = 0;
	for(i = 0; i < (cpus/COLS); i++){
	x = i;
	for (j = 0; j < (cpus/COLS); j++) {
	    if(x < 10)
		    printf("| CPU[0%d] :%21.3lf GHz",x, (mtbl.cclk_frequency_acc[x] * fraction_uq10));
	    else
		    printf("| CPU[%d] :%21.3lf GHz",x, (mtbl.cclk_frequency_acc[x] * fraction_uq10));

	    x = x + (cpus/COLS);
	    if (x > (cpus-1))
		    break;
	    printf("  ");
	}
	printf(" |\n");
	}
	printf("--------------------------------------------------------------------------------------------------");
	printf("----------------\n");

	printf("\n---------------------------------------------------------");
	printf("\n| Frequency per target XCC:\t\t\t\t|");
	printf("\n---------------------------------------------------------");
	printf("\n| XCC | GFXCLK ACC\t\t\t| GFXCLK \t|");
	printf("\n---------------------------------------------------------");
	for(i = 0; i < XCC_COUNT; i++){
		printf("\n| [%d] |  %20.3lf MHz\t| %5.3lf MHz\t| ",
				i, (mtbl.gfxclk_frequency_acc[i] * fraction_uq10),
				(mtbl.gfxclk_frequency[i] * fraction_uq10));
	}
	printf("\n---------------------------------------------------------");
	printf("\n");

	printf("\n-----------------------------------------------------------------");
	printf("\n| Max CCLK frequency supported by CPU   |  %5.3lf GHz\t\t| ",
			(mtbl.max_cclk_frequency * fraction_uq10));
	printf("\n| Min CCLK frequency supported by CPU   |  %5.3lf GHz\t\t| ",
			(mtbl.min_cclk_frequency * fraction_uq10));
	printf("\n| Max GFXCLK supported by accelerator   |  %5.3lf MHz\t\t| ",
			(mtbl.max_gfxclk_frequency * fraction_uq10));
	printf("\n| Min GFXCLK supported by accelerator   |  %5.3lf MHz\t\t| ",
			(mtbl.min_gfxclk_frequency * fraction_uq10));
	printf("\n| Max LCLK DPM state range              |  %u \t\t\t| ",
			mtbl.max_lclk_dpm_range);
	printf("\n| Min LCLK DPM state range              |  %u \t\t\t| ",
			mtbl.min_lclk_dpm_range);
	printf("\n------------------------------------------------------------------");

	printf("\n");
	printf("\n-----------------------------------------------------------------");
	printf("\n| Current operating XGMI link width     |  %5.3lf \t\t |",
			(mtbl.xgmi_width * fraction_uq10));
	printf("\n| Current operating XGMI link bitrate   |  %5.3lf Gbps\t\t |",
			(mtbl.xgmi_bitrate * fraction_uq10));
	printf("\n------------------------------------------------------------------");

	printf("\n");
	printf("\n---------------------------------------------------------------------------");
	printf("\n| XGMI Bandwidth accumulated per XGMI link in local socket\t\t  |");
	printf("\n---------------------------------------------------------------------------");
	printf("\n| Link  | \tXGMI Read BW\t\t| \tXGMI Write BW\t\t  |");
	printf("\n---------------------------------------------------------------------------");
	for(i = 0; i < NUM_XGMI_LINKS; i++){
		printf("\n| [%d] \t|  %18.3lf Gbps\t| %18.3lf Gbps\t  |",
				i, (mtbl.xgmi_read_bandwidth_acc[i] * fraction_uq10),
				(mtbl.xgmi_write_bandwidth_acc[i] * fraction_uq10));
	}
	printf("\n--------------------------------------------------------------------------");
	printf("\n");

	printf("\n--------------------------------------------------------------------------");
	printf("\n| Avg C0 residency of all enabled cores |  %18.3lf %% \t |",
            (mtbl.socket_c0_residency * fraction_uq10));
	printf("\n| Avg XCC busy for all enabled XCCs     |  %18.3lf %% \t |",
            (mtbl.socket_gfx_busy * fraction_uq10));
	printf("\n| HBM BW utilization for all HBM stacks |  %18.3lf %% \t |",
            (mtbl.dram_bandwidth_utilization * fraction_uq10));
	printf("\n| Acc value of SocketC0Residency        |  %18.3lf \t\t |",
            (mtbl.socket_c0_residency_acc * fraction_uq10));
	printf("\n| Acc value of SocketGfxBusy            |  %18.3lf \t\t |",
            (mtbl.socket_gfx_busy_acc * fraction_uq10));
	printf("\n| HBM BW for all socket HBM stacks      |  %18.3lf Gbps \t |",
            (mtbl.dram_bandwidth_acc * fraction_uq10));
	printf("\n| Max HBM BW running at max UCLK freq   |  %18.3lf Gbps \t |",
            (mtbl.max_dram_bandwidth * fraction_uq10));
	printf("\n| Acc value of Dram BW Utilization      |  %18.3lf \t\t |",
            (mtbl.dram_bandwidth_utilization_acc * fraction_uq10));
	printf("\n--------------------------------------------------------------------------");

	printf("\n");
	printf("\n--------------------------------------------------------------------------------");
	for(i = 0; i < AID_COUNT; i++)
		printf("\n| PCIe BW for devs connected to AID[%d]  |  %18.3lf Gbps\t\t| ", i,
			(mtbl.pcie_bandwidth_acc[i] * fraction_uq10));
	printf("\n--------------------------------------------------------------------------------");
	printf("\n");
	printf("\n---------------------------------------------------------");
	printf("\n| Active controllers		        | Acc value\t|");
	printf("\n---------------------------------------------------------");
	printf("\n| Prochot                               |  %-10u \t|", mtbl.prochot_residency_acc);
	printf("\n| PPT controller                        |  %-10u \t|", mtbl.ppt_residency_acc);
	printf("\n| Socket thermal throttling controller  |  %-10u \t|",
			mtbl.socket_thm_residency_acc);
	printf("\n| VR thermal throttling controller      |  %-10u \t| ",
			mtbl.vr_thm_residency_acc);
	printf("\n| HBM thermal throttling controller     |  %-10u \t| ",
			mtbl.hbm_thm_residency_acc);
	printf("\n---------------------------------------------------------\n");
	printf("\n");

	return ESMI_SUCCESS;
}

static int epyc_get_pwr_efficiency_mode(uint8_t sock_ind)
{
	esmi_status_t ret;
	uint8_t val;
	char temp_string[300];

	ret = esmi_pwr_efficiency_mode_get(sock_ind, &val);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get power efficiency mode for socket [%d] Err[%d]: %s\n",
			sock_ind, ret, esmi_get_err_msg(ret));
		return ret;
	}
	if(log_to_file && create_log_header) {
		sprintf(temp_string, "Socket%d:PowerEfficiencyMode,", sock_ind);
		append_string(&log_file_header, temp_string);
	}

	if(print_results == PRINT_RESULTS)
	{
		printf("---------------------------------------\n");
		printf("| Current power efficiency mode is %d |\n", val);
		printf("---------------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Socket,PowerEfficiencyMode\n%d,%d\n", sock_ind, val);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"PowerEfficiencyMode\":%u\n\t},", sock_ind, val);

	if(log_to_file)
	{
		sprintf(temp_string, "%d,", val);
		append_string(&log_file_data, temp_string);
	}

	return ret;
}

static int epyc_set_xgmi_pstate_range(uint8_t min, uint8_t max)
{
	esmi_status_t ret;

	ret = esmi_xgmi_pstate_range_set(min, max);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to set xGMI pstate range, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("xGMI pstate range is set successfully\n");

	return ret;

}

static int epyc_get_cpu_iso_freq_policy(uint8_t sock_ind)
{
	esmi_status_t ret;
	bool val;
	char temp_string[300];

	ret = esmi_cpurail_isofreq_policy_get(sock_ind, &val);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get Cpu Iso frequency policy for socket [%d], Err[%d]: %s\n",
			sock_ind, ret, esmi_get_err_msg(ret));
		return ret;
	}
	if(log_to_file && create_log_header) {
		sprintf(temp_string, "Socket%d:CpuIsoFrequencyPolicy,", sock_ind);
		append_string(&log_file_header, temp_string);
	}

	if(print_results == PRINT_RESULTS)
	{
		printf("--------------------------------------\n");
		printf("| Current Cpu Iso frequency policy is %d |\n", val);
		printf("--------------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Socket,CpuIsoFrequencyPolicy\n%d,%d\n", sock_ind, val);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"CpuIsoFrequencyPolicy\":%u\n\t},", sock_ind, val);

	if(log_to_file)
	{
		sprintf(temp_string, "%u,", val);
		append_string(&log_file_data, temp_string);
	}
	return ret;
}

static int epyc_set_cpu_iso_freq_policy(uint8_t sock_ind, uint8_t input)
{
	esmi_status_t ret;
	bool val;

	if (input > 1) {
		printf("Input value should be 0 or 1\n");
		return ESMI_INVALID_INPUT;
	}
	val = input;
	ret = esmi_cpurail_isofreq_policy_set(sock_ind, &val);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to set Cpu Iso frequency policy for socket [%d], Err[%d]: %s\n",
			sock_ind, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("Cpu Iso frequency policy is set successfully to value :%d\n", val);

	return ret;
}

static int epyc_set_dfc_ctrl(uint8_t sock_ind, uint8_t input)
{
	esmi_status_t ret;
	bool val;

	if (input > 1) {
		printf("Input value should be 0 or 1\n");
		return ESMI_INVALID_INPUT;
	}
	val = input;
	ret = esmi_dfc_enable_set(sock_ind, &val);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to set Data Fabric C-state enable control for socket [%d] Err[%d]: %s\n",
			sock_ind, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("Data Fabric C-state enable control is set successfully to %d\n", val);

	return ret;
}

static int epyc_get_dfc_ctrl(uint8_t sock_ind)
{
	esmi_status_t ret;
	bool val;
	char temp_string[300];

	ret = esmi_dfc_ctrl_setting_get(sock_ind, &val);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get Data Fabric C-state enable control for socket [%d] Err[%d]: %s\n",
			sock_ind, ret, esmi_get_err_msg(ret));
		return ret;
	}
	if(log_to_file && create_log_header) {
		sprintf(temp_string, "Socket%d:DataFabricCStateEnable,", sock_ind);
		append_string(&log_file_header, temp_string);
	}

	if(print_results == PRINT_RESULTS)
	{
		printf("----------------------------------------------------\n");
		printf("| Data Fabric C-state enable control value is %d |\n", val);
		printf("----------------------------------------------------\n");
	}
	else if(print_results == PRINT_RESULTS_AS_CSV)
		printf("Socket,DataFabricCStateEnable\n%d,%d\n", sock_ind, val);
	else if(print_results == PRINT_RESULTS_AS_JSON)
		printf("\n\t{\n\t\t\"Socket\":%d,\n\t\t\"DataFabricCStateEnable\":%u\n\t},", sock_ind, val);

	if(log_to_file)
	{
		sprintf(temp_string, "%u,", val);
		append_string(&log_file_data, temp_string);
	}
	return ret;
}


static char* const feat_comm[] = {
	"Output Option<s>:",
	"  -h, --help\t\t\t\t\t\t\tShow this help message",
	"  -A, --showall\t\t\t\t\t\t\tShow all esmi parameter values",
	"  -V  --version \t\t\t\t\t\tShow e-smi library version",
	"  --testmailbox [SOCKET] [VALUE<0-0xFFFFFFFF>]\t\t\tTest HSMP mailbox interface",
	"  --writemsrallowlist \t\t\t\t\t\tWrite msr-safe allowlist file",
	"  --json\t\t\t\t\t\t\tPrint output on console as json format[applicable only for get commands]",
	"  --csv\t\t\t\t\t\t\t\tPrint output on console as csv format[applicable only for get commands]",
	"  --initialdelay [INITIAL_DELAY] <TIME_RANGE<ms,s,m,h,d>>\tInitial delay before start of execution",
	"  --loopdelay    [LOOP_DELAY]    <TIME_RANGE<ms,s,m,h,d>>\tLoop delay before executing each loop",
	"  --loopcount    [LOOP_COUNT]\t\t\t\t\tSet the loop count to the specified value[pass \"-1\" for infinite loop]",
	"  --stoploop     [STOPLOOP_FILE_NAME]\t\t\t\tSet the StopLoop file name, loop will stop once the stoploop file is available",
	"  --printonconsole [ENABLE_PRINT<0-1>]\t\t\t\tPrint output on console if set to 1, or 0 to suppress the console output",
	"  --log [LOG_FILE_NAME]\t\t\t\t\t\tSet the Log file name, in which the data collected need to be logged\n",
};

static char* const feat_energy[] = {
	"Get Option<s>:",
	"  --showcoreenergy [CORE]\t\t\t\t\tShow energy for a given CPU (Joules)",
	"  --showsockenergy\t\t\t\t\t\tShow energy for all sockets (KJoules)",
};

static char* const feat_ver2_get[] = {
	"  --showsockpower\t\t\t\t\t\tShow power metrics for all sockets (Watts)",
	"  --showcorebl [CORE]\t\t\t\t\t\tShow Boostlimit for a given CPU (MHz)",
	"  --showsockc0res [SOCKET]\t\t\t\t\tShow c0_residency for a given socket (%%)",
	"  --showsmufwver\t\t\t\t\t\tShow SMU FW Version",
	"  --showhsmpdriverver\t\t\t\t\t\tShow HSMP Driver Version",
	"  --showhsmpprotover\t\t\t\t\t\tShow HSMP Protocol Version",
	"  --showprochotstatus\t\t\t\t\t\tShow HSMP PROCHOT status for all sockets",
	"  --showclocks\t\t\t\t\t\t\tShow Clock Metrics (MHz) for all sockets",
};

static char* const feat_ver2_set[] = {
	"Set Option<s>:",
	"  --setpowerlimit [SOCKET] [POWER]\t\t\t\tSet power limit"
	" for a given socket (mWatts)",
	"  --setcorebl [CORE] [BOOSTLIMIT]\t\t\t\tSet boost limit"
	" for a given core (MHz)",
	"  --setsockbl [SOCKET] [BOOSTLIMIT]\t\t\t\tSet Boost"
	" limit for a given Socket (MHz)",
	"  --apbdisable [SOCKET] [PSTATE<0-2>]\t\t\t\tSet Data Fabric"
	" Pstate for a given socket",
	"  --apbenable [SOCKET]\t\t\t\t\t\tEnable the Data Fabric performance"
	" boost algorithm for a given socket",
	"  --setxgmiwidth [MIN<0-2>] [MAX<0-2>]\t\t\t\tSet xgmi link width"
	" in a multi socket system (MAX >= MIN)",
	"  --setlclkdpmlevel [SOCKET] [NBIOID<0-3>] [MIN<0-3>] [MAX<0-3>]Set lclk dpm level"
	" for a given nbio in a given socket (MAX >= MIN)"
};

static char* const feat_ver3[] = {
	"  --showddrbw\t\t\t\t\t\t\tShow DDR bandwidth details (Gbps)",
};

static char* const feat_ver4[] = {
	"  --showsockettemp\t\t\t\t\t\tShow Temperature monitor for all sockets (°C)",
};

static char* const feat_ver5_get[] = {
	"  --showdimmtemprange [SOCKET] [DIMM_ADDR]\t\t\tShow dimm temperature range and"
	" refresh rate for a given socket and dimm address",
	"  --showdimmthermal [SOCKET] [DIMM_ADDR]\t\t\tShow dimm thermal values for a given socket"
	" and dimm address",
	"  --showdimmpower [SOCKET] [DIMM_ADDR]\t\t\t\tShow dimm power consumption for a given socket"
	" and dimm address",
	"  --showcclkfreqlimit [CORE]\t\t\t\t\tShow current clock frequency limit(MHz) for a given core",
	"  --showsvipower \t\t\t\t\t\tShow svi based power telemetry of all rails for all sockets",
	"  --showiobw [SOCKET] [LINK<P0-P3,G0-G3>]\t\t\tShow IO aggregate bandwidth for a given socket and"
	" linkname",
	"  --showlclkdpmlevel [SOCKET] [NBIOID<0-3>]\t\t\tShow lclk dpm level for a given nbio"
	" in a given socket",
	"  --showsockclkfreqlimit [SOCKET]\t\t\t\tShow current clock frequency limit(MHz) for a given socket"
};

static char* const feat_ver5_F19_M00_0F_get[] = {
	"  --showxgmibw [SOCKET] [LINK<P0-P3,G0-G3>] [BW<AGG_BW,RD_BW,WR_BW>] Show xGMI bandwidth for a given socket,"
	" linkname and bwtype"
};

static char* const feat_ver5_F19_M00_0F_set[] = {
	"  --setpowerefficiencymode [SOCKET] [MODE<0-3>]\t\t\tSet power efficiency mode"
	" for a given socket",
};

static char* const feat_ver5_F1A_M00_1F_get[] = {
	"  --showxgmibw [SOCKET] [LINK<P1,P3,G0-G3>] [BW<AGG_BW,RD_BW,WR_BW>]\tShow xGMI bandwidth for a given socket,"
	" linkname and bwtype",
	"  --showcurrpwrefficiencymode [SOCKET]\t\t\t\tShow current power effciency mode",
	"  --showcpurailisofreqpolicy [SOCKET]\t\t\t\tShow current CPU ISO frequency policy",
	"  --showdfcstatectrl [SOCKET]\t\t\t\t\tShow current DF C-state status",
};

static char* const feat_ver5_F1A_M00_1F_set[] = {
	"  --setpowerefficiencymode [SOCKET] [MODE<0-5>]\t\t\tSet power efficiency mode"
	" for a given socket",
	"  --setxgmipstaterange [MAX<0,1>] [MIN<0,1>]\t\t\tSet xgmi pstate range",
	"  --setcpurailisofreqpolicy [SOCKET] [VAL<0,1>]\t\t\tSet CPU ISO frequency policy",
	"  --dfcctrl [SOCKET] [VAL<0,1>]\t\t\t\t\tEnable or disable DF c-state"
};

static char* const feat_ver5_set[] = {
	"  --setpcielinkratecontrol [SOCKET] [CTL<0-2>]\t\t\tSet rate control for pcie link"
	" for a given socket",
	"  --setdfpstaterange [SOCKET] [MAX<0-2>] [MIN<0-2>]\t\tSet df pstate range"
	" for a given socket (MAX <= MIN)",
	"  --setgmi3linkwidth [SOCKET] [MIN<0-2>] [MAX<0-2>]\t\tSet gmi3 link width"
	" for a given socket (MAX >= MIN)",
};

static char* const feat_ver6_get[] = {
	"  --showcclkfreqlimit [CORE]\t\t\t\t\tShow current clock frequency limit(MHz) for a given core",
	"  --showsvipower \t\t\t\t\t\tShow svi based power telemetry of all rails for all sockets",
	"  --showxgmibw [SOCKET] [LINK<G0-G7>] [BW<AGG_BW,RD_BW,WR_BW>]\t\tShow xGMI bandwidth for a given socket,"
	" linkname and bwtype",
	"  --showiobw [SOCKET] [LINK<P2,P3,G0-G7>]\t\t\tShow IO aggregate bandwidth for a given socket and"
	" linkname",
	"  --showlclkdpmlevel [SOCKET] [NBIOID<0-3>]\t\t\tShow lclk dpm level for a given nbio"
	" in a given socket",
	"  --showsockclkfreqlimit [SOCKET]\t\t\t\tShow current clock frequency limit(MHz) for a given socket",
	"  --showmetrictablever\t\t\t\t\t\tShow Metrics Table Version",
	"  --showmetrictable [SOCKET]\t\t\t\t\tShow Metrics Table",
};

static char* const feat_ver6_set[] = {
	"Set Option<s>:",
	"  --setpowerlimit [SOCKET] [POWER]\t\t\t\tSet power limit"
	" for a given socket (mWatts)",
	"  --setcorebl [CORE] [BOOSTLIMIT]\t\t\t\tSet boost limit"
	" for a given core (MHz)",
	"  --setsockbl [SOCKET] [BOOSTLIMIT]\t\t\t\tSet Boost"
	" limit for a given Socket (MHz)",
	"  --setxgmiwidth [MIN<0-2>] [MAX<0-2>]\t\t\t\tSet xgmi link width"
	" in a multi socket system (MAX >= MIN)",
	"  --setlclkdpmlevel [SOCKET] [NBIOID<0-3>] [MIN<0-2>] [MAX<0-2>]Set lclk dpm level"
	" for a given nbio in a given socket (MAX >= MIN)",
};

static char* const blankline[] = {""};

static char **features = NULL;

static void show_usage(char *exe_name)
{
	int i = 0;

	printf("Usage: %s [Option]... <INPUT>...\n", exe_name);
	printf("Help : %s --help\n\n", exe_name);
	if(features != NULL)
	{
		while (features[i]) {
			printf("%s\n", features[i]);
			i++;
		}
	}
}

/*
 * returns 0 if the given string is a number, else 1
 */
static int is_string_number(char *str)
{
	int i;
	for (i = 0; str[i] != '\0'; i++) {
		if (i == 0 && str[i] == '-' && str[i + 1] != '\0') {
			continue;
		}
		if ((str[i] < '0') || (str[i] > '9')) {
			return 1;
		}
	}
	return 0;
}

/* copy the common and energy feature into features array */
static void add_comm_and_energy_feat(void)
{
	int offset = 0;

	memcpy(features, feat_comm, (ARRAY_SIZE(feat_comm) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_comm);
	memcpy(features + offset, feat_energy, (ARRAY_SIZE(feat_energy) * sizeof(char *)));
}

/* copy the hsmp proto ver2 specific features into features array */
static void add_hsmp_ver2_feat(void)
{
	int offset = ARRAY_SIZE(feat_comm) + ARRAY_SIZE(feat_energy);

	memcpy(features + offset, feat_ver2_get, (ARRAY_SIZE(feat_ver2_get) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver2_get);
	memcpy(features + offset, blankline, sizeof(char *));
	offset += 1;
	memcpy(features + offset, feat_ver2_set, (ARRAY_SIZE(feat_ver2_set) * sizeof(char *)));

	/* proto version 2 is the baseline for HSMP fetaures, so there is no addon */
	sys_info.show_addon_cpu_metrics = no_addon_cpu_metrics;
	sys_info.show_addon_socket_metrics = no_addon_socket_metrics;
	sys_info.show_addon_clock_metrics = no_addon_clock_metrics;
}

/* copy the hsmp proto ver4 specific features into features array */
static void add_hsmp_ver4_feat(void)
{
	int offset = ARRAY_SIZE(feat_comm) + ARRAY_SIZE(feat_energy);

	memcpy(features + offset, feat_ver2_get, (ARRAY_SIZE(feat_ver2_get) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver2_get);
	memcpy(features + offset, feat_ver3, (ARRAY_SIZE(feat_ver3) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver3);
	memcpy(features + offset, feat_ver4, (ARRAY_SIZE(feat_ver4) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver4);
	memcpy(features + offset, blankline, sizeof(char *));
	offset += 1;
	memcpy(features + offset, feat_ver2_set, (ARRAY_SIZE(feat_ver2_set) * sizeof(char *)));

	/* proto version 4 has extra socket metrics, cpu metrics are same as ver2 */
	sys_info.show_addon_cpu_metrics = no_addon_cpu_metrics;
	sys_info.show_addon_socket_metrics = socket_ver4_metrics;
	sys_info.show_addon_clock_metrics = no_addon_clock_metrics;
}

/* copy the hsmp proto ver5 specific features into features array */
static void add_hsmp_ver5_feat(void)
{
	int offset = ARRAY_SIZE(feat_comm) + ARRAY_SIZE(feat_energy);

	memcpy(features + offset, feat_ver2_get, (ARRAY_SIZE(feat_ver2_get) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver2_get);
	memcpy(features + offset, feat_ver3, (ARRAY_SIZE(feat_ver3) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver3);
	memcpy(features + offset, feat_ver5_get, (ARRAY_SIZE(feat_ver5_get) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver5_get);
	if (sys_info.family == 0x1A && sys_info.model <= 0x1F) {
		memcpy(features + offset, feat_ver5_F1A_M00_1F_get,
		       (ARRAY_SIZE(feat_ver5_F1A_M00_1F_get) * sizeof(char *)));
		offset += ARRAY_SIZE(feat_ver5_F1A_M00_1F_get);
	} else {
		memcpy(features + offset, feat_ver5_F19_M00_0F_get,
		       (ARRAY_SIZE(feat_ver5_F19_M00_0F_get) * sizeof(char *)));
		offset += ARRAY_SIZE(feat_ver5_F19_M00_0F_get);
	}
	memcpy(features + offset, blankline, sizeof(char *));
	offset += 1;
	memcpy(features + offset, feat_ver2_set, (ARRAY_SIZE(feat_ver2_set) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver2_set);
	memcpy(features + offset, feat_ver5_set, (ARRAY_SIZE(feat_ver5_set) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver5_set);
	if (sys_info.family == 0x1A && sys_info.model <= 0x1F) {
		memcpy(features + offset, feat_ver5_F1A_M00_1F_set,
		       (ARRAY_SIZE(feat_ver5_F1A_M00_1F_set) * sizeof(char *)));
		offset += ARRAY_SIZE(feat_ver5_F1A_M00_1F_set);
	} else {
		memcpy(features + offset, feat_ver5_F19_M00_0F_set,
		       (ARRAY_SIZE(feat_ver5_F19_M00_0F_set) * sizeof(char *)));
		offset += ARRAY_SIZE(feat_ver5_F19_M00_0F_set);
	}

	/* proto version 5 has extra socket metrics as well as extra cpu metrics */
	sys_info.show_addon_cpu_metrics = cpu_ver5_metrics;
	sys_info.show_addon_socket_metrics = socket_ver5_metrics;
	sys_info.show_addon_clock_metrics = clock_ver5_metrics;
}

/* copy the hsmp proto ver6 specific features into features array */
static void add_hsmp_ver6_feat(void)
{
	int offset = ARRAY_SIZE(feat_comm) + ARRAY_SIZE(feat_energy);

	memcpy(features + offset, feat_ver2_get, (ARRAY_SIZE(feat_ver2_get) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver2_get);
	memcpy(features + offset, feat_ver6_get, (ARRAY_SIZE(feat_ver6_get) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver6_get);
	memcpy(features + offset, blankline, sizeof(char *));
	offset += 1;
	memcpy(features + offset, feat_ver6_set, (ARRAY_SIZE(feat_ver6_set) * sizeof(char *)));

	/* version 6 cpu metrics is same as version 5 */
	sys_info.show_addon_cpu_metrics = cpu_ver5_metrics;
	sys_info.show_addon_socket_metrics = socket_ver6_metrics;
	sys_info.show_addon_clock_metrics = clock_ver5_metrics;
}

static void add_hsmp_ver7_feat(void)
{
	int offset = ARRAY_SIZE(feat_comm) + ARRAY_SIZE(feat_energy);

	/* copy all the "get" messages */
	memcpy(features + offset, feat_ver2_get, (ARRAY_SIZE(feat_ver2_get) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver2_get);
	memcpy(features + offset, feat_ver3, (ARRAY_SIZE(feat_ver3) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver3);
	memcpy(features + offset, feat_ver5_get, (ARRAY_SIZE(feat_ver5_get) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver5_get);
	memcpy(features + offset, feat_ver5_F1A_M00_1F_get,
	       (ARRAY_SIZE(feat_ver5_F1A_M00_1F_get) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver5_F1A_M00_1F_get);

	memcpy(features + offset, blankline, sizeof(char *));
	offset += 1;

	/* copy all the "set" messages */
	memcpy(features + offset, feat_ver2_set, (ARRAY_SIZE(feat_ver2_set) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver2_set);
	memcpy(features + offset, feat_ver5_set, (ARRAY_SIZE(feat_ver5_set) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver5_set);
	memcpy(features + offset, feat_ver5_F1A_M00_1F_set,
			(ARRAY_SIZE(feat_ver5_F1A_M00_1F_set) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver5_F1A_M00_1F_set);

	/* proto version 7 metrics are same as version 5 */
	sys_info.show_addon_cpu_metrics = cpu_ver5_metrics;
	sys_info.show_addon_socket_metrics = socket_ver5_metrics;
	sys_info.show_addon_clock_metrics = clock_ver5_metrics;
}

static esmi_status_t init_proto_version_func_pointers()
{
	uint32_t proto_ver;
	esmi_status_t ret;
	int size;

	ret = esmi_hsmp_proto_ver_get(&proto_ver);
	if (ret) {
		printf(RED "Error in initialising HSMP version sepcific info, "
			"Only energy data can be obtained...\nErr[%d]: %s\n\n" RESET,
			ret, esmi_get_err_msg(ret));
		size = ARRAY_SIZE(feat_energy) + ARRAY_SIZE(feat_comm);
		features = malloc((size + 1) * sizeof(char *));
		if (!features)
			return ESMI_NO_MEMORY;
		add_comm_and_energy_feat();
		/* Indicate the array end with NULL pointer */
		features[size] = NULL;
		return ESMI_SUCCESS;
	}

	switch (proto_ver) {
	case 2:
		size = ARRAY_SIZE(feat_comm) + ARRAY_SIZE(feat_energy) +
		       ARRAY_SIZE(feat_ver2_get) + ARRAY_SIZE(feat_ver2_set) +
		       ARRAY_SIZE(blankline);
		features = malloc((size + 1) * sizeof(char *));
		if (!features)
			return ESMI_NO_MEMORY;
		add_comm_and_energy_feat();
		add_hsmp_ver2_feat();
		break;
	case 4:
		size = ARRAY_SIZE(feat_comm) + ARRAY_SIZE(feat_ver2_get) +
		       ARRAY_SIZE(feat_ver2_set) + ARRAY_SIZE(feat_ver4) +
		       ARRAY_SIZE(feat_energy) + ARRAY_SIZE(feat_ver3) + ARRAY_SIZE(blankline);
		features = malloc((size + 1) * sizeof(char *));
		if (!features)
			return ESMI_NO_MEMORY;
		add_comm_and_energy_feat();
		add_hsmp_ver4_feat();
		break;
	case 5:
		size = ARRAY_SIZE(feat_comm) + ARRAY_SIZE(feat_ver2_get) +
		       ARRAY_SIZE(feat_ver2_set) + ARRAY_SIZE(feat_ver5_get) +
		       ARRAY_SIZE(feat_ver5_set) + ARRAY_SIZE(feat_ver3) +
		       ARRAY_SIZE(feat_energy) + ARRAY_SIZE(blankline);
		if (sys_info.family == 0x1A && sys_info.model <= 0x1f) {
			size += ARRAY_SIZE(feat_ver5_F1A_M00_1F_get);
			size += ARRAY_SIZE(feat_ver5_F1A_M00_1F_set);
		} else {
			size += ARRAY_SIZE(feat_ver5_F19_M00_0F_get);
			size += ARRAY_SIZE(feat_ver5_F19_M00_0F_set);
		}
		features = malloc((size + 1) * sizeof(char *));
		if (!features)
			return ESMI_NO_MEMORY;
		add_comm_and_energy_feat();
		add_hsmp_ver5_feat();
		break;
	case 6:
		size = ARRAY_SIZE(feat_comm) + ARRAY_SIZE(feat_ver2_get) +
		       ARRAY_SIZE(feat_energy) + ARRAY_SIZE(feat_ver6_get) +
		       ARRAY_SIZE(feat_ver6_set) + ARRAY_SIZE(blankline);
		features = malloc((size + 1) * sizeof(char *));
		if (!features)
			return ESMI_NO_MEMORY;
		add_comm_and_energy_feat();
		add_hsmp_ver6_feat();
		break;
	case 7:
	default:
		if (proto_ver > 7) {
			printf(MAG "This version of the library does not fully support your platform.\n");
			printf(MAG "defaulting to highest supported platform feature set, may support common features across platform.\n");
			printf(MAG "Please upgrade the library.\n\n" RESET);
		}
		size = ARRAY_SIZE(feat_comm) + ARRAY_SIZE(feat_ver2_get) +
		       ARRAY_SIZE(feat_ver2_set) + ARRAY_SIZE(feat_ver5_get) +
		       ARRAY_SIZE(feat_ver5_set) + ARRAY_SIZE(feat_ver3) +
		       ARRAY_SIZE(feat_energy) + ARRAY_SIZE(blankline) +
		       ARRAY_SIZE(feat_ver5_F1A_M00_1F_get) +
		       ARRAY_SIZE(feat_ver5_F1A_M00_1F_set);
		features = malloc((size + 1) * sizeof(char *));
		if (!features)
			return ESMI_NO_MEMORY;
		add_comm_and_energy_feat();
		add_hsmp_ver7_feat();
		break;
	}

	/* Indicate the array end with NULL pointer */
	features[size] = NULL;

	return ESMI_SUCCESS;
}

static void print_esmi_version()
{

	printf("-----------------------------------------------------------\n");
	printf("| E-smi library version  |  %d.%d.%d build: %-10s \t |\n",
	       e_smi_VERSION_MAJOR, e_smi_VERSION_MINOR, e_smi_VERSION_PATCH, e_smi_VERSION_BUILD);
	printf("-----------------------------------------------------------\n");
}

/**
Parse command line parameters and set data for program.
@param argc number of command line parameters
@param argv list of command line parameters
*/
static int parsesmi_args(int argc,char **argv)
{
	int ret = ESMI_INVALID_INPUT;
	int i;
	int opt = 0; /* option character */
	uint32_t core_id = 0, sock_id = 0;
	uint32_t power = 0, boostlimit = 0;
	int32_t pstate;
	static char *args[ARGS_MAX];
	char sudostr[] = "sudo";
	uint8_t min, max;
	uint8_t nbio_id;
	char *end;
	char *link_name;
	char *bw_type;
	uint8_t ctrl, mode, value;
	uint64_t input_data;

	//Specifying the expected options
	static struct option long_options[] = {
		{"help",                no_argument,		0,	'h'},
		{"showall",             no_argument,		0,	'A'},
		{"testmailbox",         required_argument,	0,	'N'},
		{"showcoreenergy",      required_argument,	0,	'e'},
		{"showsockenergy",	no_argument,		0,	's'},
		{"showsockpower",	no_argument,		0,	'p'},
		{"showsmufwver",	no_argument,		0,	'f'},
		{"showhsmpdriverver",	no_argument,		0,	'o'},
		{"showcorebl",		required_argument,	0,	'L'},
		{"setpowerlimit",	required_argument,	0,	'C'},
		{"setcorebl",		required_argument,	0,	'a'},
		{"setsockbl",		required_argument,	0,	'b'},
		{"showsockc0resi",	required_argument,	0,	'r'},
		{"showddrbw",		no_argument,		0,	'd'},
		{"showsockettemp",	no_argument,		0,      't'},
		{"showhsmpprotover",	no_argument,		0,	'v'},
		{"showprochotstatus",	no_argument,		0,	'x'},
		{"apbenable",		required_argument,	0,	'y'},
		{"apbdisable",		required_argument,	0,	'u'},
		{"showclocks",		no_argument,		0,	'z'},
		{"setxgmiwidth",	required_argument,	0,	'w'},
		{"setlclkdpmlevel",	required_argument,	0,	'l'},
		{"showdimmthermal",		required_argument,	0,	'H'},
		{"showdimmpower",		required_argument,	0,	'g'},
		{"showdimmtemprange",		required_argument,	0,	'T'},
		{"showcclkfreqlimit",		required_argument,	0,	'q'},
		{"showsvipower",		no_argument,		0,	'm'},
		{"showiobw",			required_argument,	0,	'B'},
		{"showxgmibw",			required_argument,	0,	'i'},
		{"setpcielinkratecontrol",	required_argument,	0,	'j'},
		{"setpowerefficiencymode",	required_argument,	0,	'k'},
		{"setdfpstaterange",		required_argument,	0,	'X'},
		{"setgmi3linkwidth",		required_argument,	0,	'n'},
		{"showlclkdpmlevel",		required_argument,	0,	'Y'},
		{"showsockclkfreqlimit",	required_argument,	0,	'Q'},
		{"showmetrictablever",		no_argument,		0,	'D'},
		{"showmetrictable",		required_argument,	0,	'J'},
		{"version",			no_argument,		0,	'V'},
		{"writemsrallowlist",		no_argument,		0,	'W'},
		{"showcurrpwrefficiencymode", 	required_argument, 	0, 	'O'},
		{"showcpurailisofreqpolicy", 	required_argument, 	0, 	'K'},
		{"showdfcstatectrl", 		required_argument, 	0, 	'M'},
		{"setxgmipstaterange", 		required_argument, 	0, 	'E'},
		{"setcpurailisofreqpolicy", 	required_argument, 	0, 	'F'},
		{"dfcctrl", 			required_argument, 	0, 	'P'},
		{"json", 			no_argument,	 	&flag, 	12},
		{"csv", 			no_argument,	 	&flag, 	13},
		{"initialdelay", 		required_argument, 	&flag, 	14},
		{"loopdelay", 			required_argument, 	&flag, 	15},
		{"loopcount", 			required_argument, 	&flag, 	16},
		{"printonconsole", 		required_argument, 	&flag, 	17},
		{"log", 			required_argument, 	&flag, 	18},
		{"stoploop", 			required_argument, 	&flag, 	19},
		{0,				0,			0,	0},
	};

	int long_index = 0;
	char *helperstring = "+hAV";

	optind = 0;
	char* err_string = NULL;
	int ret_on_err = ESMI_INVALID_INPUT;
	bool show_usage_on_err = false;
	while ((opt = getopt_long(argc, argv, helperstring,
			long_options, &long_index)) != -1)
	{
		char temp_string[300];
		if (opt == ':')
		{
			/* missing option argument */
			sprintf(temp_string, "%s: option '-%c' requires an argument.\n\n", argv[0], opt);
			append_string(&err_string, temp_string);
			break;
		}
		else if (opt == '?')
		{
			if (isprint(opt)) {
				sprintf(temp_string, "Try `%s --help' for more information.\n", argv[0]);
			} else {
				sprintf(temp_string, "Unknown option character `\\x%x'.\n", opt);
			}
			append_string(&err_string, temp_string);
			break;
		}

		if (opt == 'e' || opt == 'y' || opt == 'L' || opt == 'r' || opt == 'q' || opt == 'Q' || opt == 'J' ||
			opt == 'I' || opt == 'O' || opt == 'M' || opt == 'K' || opt == 'c' ||
			opt == 'N' || opt == 'C' || opt == 'a' || opt == 'b' || opt == 'u' || opt == 'w' || opt == 'H' || opt == 'g' || opt == 'T' ||
			opt == 'B' || opt == 'j' || opt == 'k' || opt == 'Y' || opt == 'G' || opt == 'E' || opt == 'F' || opt == 'P' ||
			opt == 'l')
		{
			if (is_string_number(optarg))
			{
				sprintf(temp_string, "Option '--%s' require a valid numeric value as an argument\n\n", long_options[long_index].name);
				append_string(&err_string, temp_string);
				show_usage_on_err = true;
				break;
			}
			if (*optarg == '-') {
				sprintf(temp_string, "Option '--%s', Negative values are not accepted\n\n", long_options[long_index].name);
				append_string(&err_string, temp_string);
				break;
			}
		}
		if (opt == 'N' || opt == 'C' || opt == 'a' || opt == 'b' || opt == 'u' || opt == 'w' || opt == 'H' || opt == 'g' || opt == 'T' ||
			opt == 'j' || opt == 'k' || opt == 'Y' || opt == 'E' || opt == 'F' || opt == 'P')
		{
			// make sure optind is valid  ... or another option
			if (optind >= argc) {
				sprintf(temp_string, "\nOption '--%s' require TWO arguments <index>  <set_value>\n\n", long_options[long_index].name);
				append_string(&err_string, temp_string);
				show_usage_on_err = true;
				break;
			}
			if (opt == 'N' || opt == 'H' || opt == 'g' || opt == 'T')
			{
				if (*argv[optind] == '-') {
					sprintf(temp_string, "\nOption '--%s' requires TWO arguments and value should be non negative\n\n", long_options[long_index].name);
					append_string(&err_string, temp_string);
					show_usage_on_err = true;
					break;
				}
				if (!strncmp(argv[optind], "0x", 2) || !strncmp(argv[optind], "0X", 2))
				{
					input_data = strtoul(argv[optind++], &end, 16);
					if (*end) {
						sprintf(temp_string, "Option '--%s' requires 2nd argument as valid numeric value\n\n", long_options[long_index].name);
						append_string(&err_string, temp_string);
						show_usage_on_err = true;
						break;
					}
				} else {
					if (is_string_number(argv[optind])) {
						sprintf(temp_string, "Option '--%s' requires 2nd argument as valid numeric value\n\n", long_options[long_index].name);
						append_string(&err_string, temp_string);
						show_usage_on_err = true;
						break;
					}
					input_data = atol(argv[optind++]);
				}
			} else {
				if (is_string_number(argv[optind])) {
					sprintf(temp_string, "Option '--%s' requires 2nd argument as valid numeric value\n\n", long_options[long_index].name);
					append_string(&err_string, temp_string);
					show_usage_on_err = true;
					break;
				}
				if (*argv[optind] == '-') {
					sprintf(temp_string, "Option '--%s', Negative values are not accepted\n\n", long_options[long_index].name);
					append_string(&err_string, temp_string);
					break;
				}
				if (opt == 'C' || opt == 'a' || opt == 'b' || opt == 'u' || opt == 'w' ||
					opt == 'j' || opt == 'k' || opt == 'Y' || opt == 'E' || opt == 'F' || opt == 'P')
				{
					optind++;//Adding Extra Arguments
				}
			}
		}
		if (opt == 'B')
		{
			if ((optind >= argc) || (*optarg == '-') || (*argv[optind] == '-')) {
				sprintf(temp_string, "\nOption '--%s' requires two valid arguments <arg1> <arg2>\n\n", long_options[long_index].name);
				append_string(&err_string, temp_string);
				show_usage_on_err = true;
				break;
			}
			if (opt == 'B') {
				if (is_string_number(optarg) || !is_string_number(argv[optind])) {
					sprintf(temp_string, "Option '--%s', Please provide valid link names.\n", long_options[long_index].name);
					append_string(&err_string, temp_string);
					break;
				}
			}
			optind++;//Adding Extra Arguments
		}
		if (opt == 'G')
		{
			if ((optind) >= argc || *argv[optind] == '-') {
				sprintf(temp_string, "\nOption '--%s' requires two arguments <arg1> <arg2>\n\n", long_options[long_index].name);
				append_string(&err_string, temp_string);
				show_usage_on_err = true;
				break;
			}

			if (is_string_number(argv[optind])) {
				sprintf(temp_string, "Option '--%s' requires 2nd value as valid numeric value\n\n", long_options[long_index].name);
				append_string(&err_string, temp_string);
				show_usage_on_err = true;
				break;
			}
			optind++;//Adding Extra Arguments
		}
		if (opt == 'l') {
			// make sure optind is valid  ... or another option
			if ((optind + 2) >= argc ) {
				sprintf(temp_string, "\nOption '--%s' requires FOUR arguments <socket> <nbioid> <min_value> <max_value>\n\n", long_options[long_index].name);
				append_string(&err_string, temp_string);
				show_usage_on_err = true;
				break;
			}

			if (is_string_number(argv[optind]) || is_string_number(argv[optind + 1]) || is_string_number(argv[optind + 2])) {
				sprintf(temp_string, "Option '--%s' requires 2nd, 3rd, 4th argument as valid numeric value\n\n", long_options[long_index].name);
				append_string(&err_string, temp_string);
				show_usage_on_err = true;
				break;
			}
			if (*argv[optind] == '-' || *argv[optind + 1] == '-' || *argv[optind + 2] == '-') {
				sprintf(temp_string, "Option '--%s', Negative values are not accepted\n", long_options[long_index].name);
				append_string(&err_string, temp_string);
				break;
			}
			optind++;//Adding Extra Arguments
			optind++;//Adding Extra Arguments
			optind++;//Adding Extra Arguments
		}
		if (opt == 'i')
		{
			if (((optind + 1) >= argc) || (*optarg == '-') || (*argv[optind + 1] == '-')) {
				sprintf(temp_string, "\nOption '--%s' requires three valid arguments <socket> <Link> <BW>\n\n", long_options[long_index].name);
				append_string(&err_string, temp_string);
				show_usage_on_err = true;
				break;
			}
			if (opt == 'i') {
				if (is_string_number(optarg) || !is_string_number(argv[optind]) || !is_string_number(argv[optind + 1])) {
					sprintf(temp_string, "Option '--%s', Please provide valid arguments <socket> <Link> <BW>.\n", long_options[long_index].name);
					append_string(&err_string, temp_string);
					break;
				}
			}
			optind++;//Adding Extra Arguments
			optind++;//Adding Extra Arguments
		}
		if ((opt == 'n') || (opt == 'X')) {
			// make sure optind is valid  ... or another option
			if ((optind + 1) >= argc || *argv[optind] == '-' || *argv[optind + 1] == '-') {
				sprintf(temp_string, "\nOption '--%s' requires THREE arguments <socket> <min_value> <max_value>\n\n", long_options[long_index].name);
				append_string(&err_string, temp_string);
				show_usage_on_err = true;
				break;
			}
			if (is_string_number(argv[optind]) || is_string_number(argv[optind + 1])) {
				sprintf(temp_string, "Option '--%s' requires 2nd, 3rd, as valid numeric value\n\n", long_options[long_index].name);
				append_string(&err_string, temp_string);
				show_usage_on_err = true;
				break;
			}
			optind++;//Adding Extra Arguments
			optind++;//Adding Extra Arguments
		}

		if ((opt == 0) &&
			((*long_options[long_index].flag == 12) || (*long_options[long_index].flag == 13) || (*long_options[long_index].flag == 14) ||
			 (*long_options[long_index].flag == 15) || (*long_options[long_index].flag == 16) || (*long_options[long_index].flag == 17) ||
			 (*long_options[long_index].flag == 18) || (*long_options[long_index].flag == 19)))
		{
			if (*long_options[long_index].flag == 12) {
				if(print_results != DONT_PRINT_RESULTS)
					print_results = PRINT_RESULTS_AS_JSON;
			}
			else if (*long_options[long_index].flag == 13) {
				if(print_results != DONT_PRINT_RESULTS)
					print_results = PRINT_RESULTS_AS_CSV;
			}
			else if ((*long_options[long_index].flag == 14) || (*long_options[long_index].flag == 15) || (*long_options[long_index].flag == 16) ||
					 (*long_options[long_index].flag == 17))
			{
				if (is_string_number(optarg)) {
					sprintf(temp_string, "Option '--%s' require a valid numeric value as an argument\n\n", long_options[long_index].name);
					append_string(&err_string, temp_string);
					break;
				}
				if(*long_options[long_index].flag == 16)
				{
					if (*optarg == '-') {
						int arg_passed = atoi(optarg);
						if (arg_passed != -1) {
							sprintf(temp_string, "Option '--%s', Negative values are not accepted, except -1 for infinite loopcount\n\n", long_options[long_index].name);
							append_string(&err_string, temp_string);
							break;
						}
					}
				}
				else
				{
					if (*optarg == '-') {
						sprintf(temp_string, "Option '--%s', Negative values are not accepted\n\n", long_options[long_index].name);
						append_string(&err_string, temp_string);
						break;
					}
				}
			}

			if (*long_options[long_index].flag == 14) {
				initial_delay_in_secs = atoi(optarg);
			}
			else if (*long_options[long_index].flag == 15) {
				loop_delay_in_secs = atoi(optarg);
				loop_delay_in_secs_passed = true;
			}
			else if (*long_options[long_index].flag == 16) {
				loop_count = atoi(optarg);
			}
			else if (*long_options[long_index].flag == 17) {
				uint32_t temp = atoi(optarg);
				if((temp != 0) && (print_results == DONT_PRINT_RESULTS))
					print_results = PRINT_RESULTS;
				else if (temp == 0)
					print_results = DONT_PRINT_RESULTS;
			}
			else if (*long_options[long_index].flag == 18) {
				append_string(&log_file_name, optarg);
				log_to_file = LOG_TO_FILE;
				create_log_header = true;
			}
			else if (*long_options[long_index].flag == 19) {
				append_string(&stoploop_file_name, optarg);
			}

			if ((*long_options[long_index].flag == 14) || (*long_options[long_index].flag == 15)) {
				if (optind < argc) {
					double temp_var = 0;
					if (!strncmp(argv[optind], "ms", 2) || !strncmp(argv[optind], "MS", 2) || !strncmp(argv[optind], "Ms", 2) || !strncmp(argv[optind], "mS", 2)) {
						temp_var = .001;
					}
					else if (!strncmp(argv[optind], "s", 1) || !strncmp(argv[optind], "S", 1)) {
						temp_var = 1;
					}
					else if (!strncmp(argv[optind], "m", 1) || !strncmp(argv[optind], "M", 1)) {
						temp_var = 60;
					}
					else if (!strncmp(argv[optind], "h", 1) || !strncmp(argv[optind], "H", 1)) {
						temp_var = 3600;
					}
					else if (!strncmp(argv[optind], "d", 1) || !strncmp(argv[optind], "D", 1)) {
						temp_var = 86400;
					}

					if (0 != temp_var) {
						if((*long_options[long_index].flag == 14) && initial_delay_in_secs) initial_delay_in_secs = initial_delay_in_secs*temp_var;
						if((*long_options[long_index].flag == 15) && loop_delay_in_secs) loop_delay_in_secs = loop_delay_in_secs*temp_var;
						optind++;
					}
				}
			}
		}

		if (getuid() != 0) {
			switch (opt) {
				/* Below options requires sudo permissions to run */
				case 'C':
				case 'A':
				case 'a':
				case 'b':
				case 'c':
				case 'e':
				case 's':
				case 'y':
				case 'u':
				case 'w':
				case 'l':
				case 'k':
				case 'j':
				case 'X':
				case 'W':
				case 'n':
				case 'E':
				case 'F':
				case 'P':
					args[0] = sudostr;
					args[1] = argv[0];
					for (i = 0; i < argc; i++) {
						args[i + 1] = argv[i];
					}
					args[i + 1] = NULL;
					execvp("sudo", args);
			}
		}
	}

	show_smi_message();
	if(err_string)
	{
		printf(MAG "%s" RESET, err_string);

		if(show_usage_on_err)
			show_usage(argv[0]);

		free(err_string);
		err_string = NULL;

		return ret_on_err;
	}

	/* smi monitor objects initialization */
	ret = esmi_init();
	if(ret != ESMI_SUCCESS) {
		printf(RED "\tESMI Not initialized, drivers not found.\n"
		       "\tErr[%d]: %s\n" RESET, ret, esmi_get_err_msg(ret));
		return ret;
	}

	ret = cache_system_info();
	if(ret != ESMI_SUCCESS) {
		printf(RED "\tError in reading system info.\n"
		       "\tErr[%d]: %s\n" RESET, ret, esmi_get_err_msg(ret));
		return ret;
	}

	ret = init_proto_version_func_pointers();
	if (ret != ESMI_SUCCESS) {
		printf(RED "\tError in allocating memory \n"
		       "\tErr[%d]: %s\n" RESET, ret, esmi_get_err_msg(ret));
		return ret;
	}

	if (argc <= 1) {
		ret = show_smi_all_parameters();
		printf(MAG"\nTry `%s --help' for more information." RESET
			"\n\n", argv[0]);
	}

	if(initial_delay_in_secs) {
		if(print_results == PRINT_RESULTS) printf("\n* InitialDelay(in secs):%f, ...\n", initial_delay_in_secs);
		sleep_in_milliseconds(initial_delay_in_secs*1000);
	}
	int loop_counter = 0;
	char date_buffer[100];
	char time_buffer[100];

	do {
	FILE *log_file_fp =  NULL;
	if(log_file_name) {
		log_file_fp = fopen(log_file_name, "a");
		if(log_file_fp == NULL) {
			printf("Error: Failed to open log file '%s'", log_file_name);
			return ESMI_FILE_ERROR;
		}
	}
	FILE *stoploop_file_fp =  NULL;
	if(stoploop_file_name && loop_count) {
		stoploop_file_fp = fopen(stoploop_file_name, "r");
		if(stoploop_file_fp != NULL) {
			if(print_results == PRINT_RESULTS) printf("stoploop file \"%s\" is present, hence stopped loop\n", stoploop_file_name);
			break;
		}
	}

	if(loop_count) {
		if(loop_delay_in_secs_passed == false) loop_delay_in_secs = 1;
		if(print_results == PRINT_RESULTS) printf("\n* LoopCount:%d, LoopDelay(in secs):%f, ...\n",loop_counter, loop_delay_in_secs);
		if(loop_delay_in_secs) sleep_in_milliseconds(loop_delay_in_secs*1000);
	}

	struct timeval tp;
	gettimeofday(&tp, NULL);
	long int ms = tp.tv_usec / 1000;

	time_t current_time = time (0);
	strftime (date_buffer, 100, "%Y-%m-%d", localtime (&current_time));
	strftime (time_buffer, 100, "%H:%M:%S", localtime (&current_time));
	char temp_string[100] = {'\0'};
	sprintf(temp_string, ":%lu", ms);
	strcat(time_buffer, temp_string);

	if(loop_count) {
		if(print_results == PRINT_RESULTS) printf ("* CurrentTime:%s,%s\n", date_buffer, time_buffer);
	}

	optind = 0;
	while ((opt = getopt_long(argc, argv, helperstring,
			long_options, &long_index)) != -1) {

	if (opt == 'N' || opt == 'H' || opt == 'g' || opt == 'T')
	{
		if (!strncmp(argv[optind], "0x", 2) || !strncmp(argv[optind], "0X", 2))
		{
			input_data = strtoul(argv[optind++], &end, 16);//Adding Extra Arguments
		} else {
			input_data = atol(argv[optind++]);//Adding Extra Arguments
		}
	}
	else if(opt == 0 && (*long_options[long_index].flag == 14 || *long_options[long_index].flag == 15))
	{
		if (optind < argc) {
			if (!strncmp(argv[optind], "ms", 2) || !strncmp(argv[optind], "MS", 2) || !strncmp(argv[optind], "Ms", 2) || !strncmp(argv[optind], "mS", 2) ||
				!strncmp(argv[optind], "s", 1) || !strncmp(argv[optind], "s", 1) ||
				!strncmp(argv[optind], "m", 1) || !strncmp(argv[optind], "M", 1) ||
				!strncmp(argv[optind], "h", 1) || !strncmp(argv[optind], "H", 1) ||
				!strncmp(argv[optind], "d", 1) || !strncmp(argv[optind], "D", 1)) {
				optind++;//Adding Extra Arguments
			}
		}
	}
	switch (opt) {
		case 0:
			ret = ESMI_SUCCESS;
			break;
		case 'e' :
			/* Get the energy for a given core index */
			core_id = atoi(optarg);
			ret = epyc_get_coreenergy(core_id);
			break;
		case 's' :
			/* Get the energy metrics for all sockets */
			ret = epyc_get_sockenergy();
			break;
		case 'p' :
			/* Get the Power metrics for all sockets */
			ret = epyc_get_socketpower();
			break;
		case 'd' :
			/* Get DDR bandwidth details */
			ret = epyc_get_ddr_bw();
			break;
		case 'f' :
			/* Get SMU Firmware version */
			ret = epyc_get_smu_fw_version();
			break;
		case 'o' :
			/* Get HSMP driver version */
			ret = epyc_get_hsmp_driver_version();
			break;
		case 'v' :
			/* Get HSMP protocol version */
			ret = epyc_get_hsmp_proto_version();
			break;
		case 'x' :
			/* Get HSMP PROCHOT status */
			ret = epyc_get_prochot_status();
			break;
		case 'y' :
			sock_id = atoi(optarg);
			/* Set auto DF Pstate */
			ret = epyc_apb_enable(sock_id);
			break;
		case 'u' :
			sock_id = atoi(optarg);
			pstate = atoi(argv[optind++]);
			/* Set DF Pstate to user specified value */
			ret = epyc_set_df_pstate(sock_id, pstate);
			break;
		case 'z' :
			/* Get Clock Frequencies */
			ret = epyc_get_clock_freq();
			break;
		case 'L' :
			/* Get the Boostlimit for a given core index */
			core_id = atoi(optarg);
			ret = epyc_get_coreperf(core_id);
			break;
		case 'C':
			/* Set the power limit value for a given socket */
			sock_id = atoi(optarg);
			power = atoi(argv[optind++]);
			ret = epyc_setpowerlimit(sock_id, power);
			break;
		case 'a' :
			/* Set the boostlimit value for a given core */
			core_id = atoi(optarg);
			boostlimit = atoi(argv[optind++]);
			ret = epyc_setcoreperf(core_id, boostlimit);
			break;
		case 'b' :
			/* Set the boostlimit value for a given socket */
			sock_id = atoi(optarg);
			boostlimit = atoi(argv[optind++]);
			ret = epyc_setsocketperf(sock_id, boostlimit);
			break;
		case 'r' :
			/* Get the Power values for a given socket */
			sock_id = atoi(optarg);
			ret = epyc_get_sockc0_residency(sock_id);
			break;
		case 't' :
			/* Get socket Temperature*/
			ret = epyc_get_temperature();
			break;

		case 'w' :
			/* Set xgmi link width */
			min = atoi(optarg);
			max = atoi(argv[optind++]);
			ret = epyc_set_xgmi_width(min, max);
			break;
		case 'l' :
			/* Set lclk dpm level */
			sock_id = atoi(optarg);
			nbio_id = atoi(argv[optind++]);
			min = atoi(argv[optind++]);
			max = atoi(argv[optind++]);
			ret = epyc_set_lclk_dpm_level(sock_id, nbio_id, min, max);
			break;
		case 'g' :
			/* Get DIMM power consumption */
			sock_id = atoi(optarg);
			ret = epyc_get_dimm_power(sock_id, input_data);
			break;
		case 'T' :
			/* Get DIMM temp range and refresh rate */
			sock_id = atoi(optarg);
			ret = epyc_get_dimm_temp_range_refresh_rate(sock_id, input_data);
			break;
		case 'H' :
			/* Get DIMM temperature */
			sock_id = atoi(optarg);
			ret = epyc_get_dimm_thermal(sock_id, input_data);
			break;
		case 'q' :
			/* Get the current clock freq limit for a given core */
			core_id = atoi(optarg);
			ret = epyc_get_curr_freq_limit_core(core_id);
			break;
		case 'm' :
			/* Get svi based power telemetry of all rails */
			epyc_get_power_telemetry();
			break;
		case 'i' :
			/* Get xgmi bandiwdth info on specified link */
			sock_id = atoi(optarg);
			link_name = argv[optind++];
			bw_type = argv[optind++];
			epyc_get_xgmi_bandwidth_info(sock_id, link_name, bw_type);
			break;
		case 'B' :
			/* Get io bandiwdth info on specified link */
			sock_id = atoi(optarg);
			link_name = argv[optind++];
			ret = epyc_get_io_bandwidth_info(sock_id, link_name);
			break;
		case 'n' :
			/* Set gmi3 link width */
			sock_id = atoi(optarg);
			min = atoi(argv[optind++]);
			max = atoi(argv[optind++]);
			ret = epyc_set_gmi3_link_width(sock_id, min, max);
			break;
		case 'j' :
			/* Set rate control on pci3 gen5 capable devices */
			sock_id = atoi(optarg);
			ctrl = atoi(argv[optind++]);
			ret = epyc_set_pciegen5_rate_ctl(sock_id, ctrl);
			break;
		case 'k' :
			/* Set power efficiecny profile policy */
			sock_id = atoi(optarg);
			mode = atoi(argv[optind++]);
			ret = epyc_set_power_efficiency_mode(sock_id, mode);
			break;
		case 'X' :
			/* Set df pstate range */
			sock_id = atoi(optarg);
			max = atoi(argv[optind++]);
			min = atoi(argv[optind++]);
			ret = epyc_set_df_pstate_range(sock_id, max, min);
			break;
		case 'Y' :
			/* Get lclk DPM level for a given nbio and a socket */
			sock_id = atoi(optarg);
			nbio_id = atoi(argv[optind++]);
			epyc_get_lclk_dpm_level(sock_id, nbio_id);
			break;
		case 'Q' :
			/* Get the current clock freq limit for a given socket */
			sock_id = atoi(optarg);
			ret = epyc_get_curr_freq_limit_socket(sock_id);
			break;
		case 'D' :
			/* Get Metrics Table version */
			ret = epyc_get_metrics_table_version();
			break;
		case 'J' :
			/* Get Metrics Table */
			sock_id = atoi(optarg);
			ret = epyc_show_metrics_table(sock_id);
			break;
		case 'N' :
			/* Test HSMP mailbox i/f */
			sock_id = atoi(optarg);
			if (input_data > UINT_MAX) {
				printf(RED "%s: option '-%c' requires input argument"
					" to be less than 0x%x"
					RESET "\n\n", argv[0], opt, UINT_MAX);
				break;
			}
			ret = test_hsmp_mailbox(sock_id, input_data);
			break;
		case 'O' :
			sock_id = atoi(optarg);
			ret = epyc_get_pwr_efficiency_mode(sock_id);
			break;
		case 'E' :
			min = atoi(optarg);
			max = atoi(argv[optind++]);
			ret = epyc_set_xgmi_pstate_range(min, max);
			break;
		case 'F' :
			sock_id = atoi(optarg);
			value = atoi(argv[optind++]);
			ret = epyc_set_cpu_iso_freq_policy(sock_id, value);
			break;
		case 'P' :
			sock_id = atoi(optarg);
			value = atoi(argv[optind++]);
			ret = epyc_set_dfc_ctrl(sock_id, value);
			break;
		case 'M' :
			sock_id = atoi(optarg);
			ret = epyc_get_dfc_ctrl(sock_id);
			break;
		case 'K' :
			sock_id = atoi(optarg);
			ret = epyc_get_cpu_iso_freq_policy(sock_id);
			break;
		case 'A' :
			ret = show_smi_all_parameters();
			ret = ESMI_SUCCESS;
			break;
		case 'h' :
			show_usage(argv[0]);
			ret = ESMI_SUCCESS;
			break;
		case 'V' :
			print_esmi_version();
			ret = ESMI_SUCCESS;
			break;
		case 'W' :
			write_msr_allowlist_file();
			ret = ESMI_SUCCESS;
			break;
		case ':' :
			/* missing option argument */
			printf(RED "%s: option '-%c' requires an argument."
				RESET "\n\n", argv[0], opt);
			break;
		case '?':
			if (isprint(opt)) {
				printf(MAG "Try `%s --help' for more"
				" information." RESET "\n", argv[0]);
			} else {
				printf("Unknown option character"
				" `\\x%x'.\n", opt);
			}
			break;
		default:
			printf(MAG "Try `%s --help' for more information."
						RESET "\n\n", argv[0]);
			break;
		} // end of Switch
	}
	if (optind < argc) {
		printf(RED"\nExtra Non-option argument<s> passed : %s"
			RESET "\n", argv[optind]);
		printf(MAG "Try `%s --help' for more information."
					RESET "\n\n", argv[0]);
	}
	if(log_file_fp)
	{
		if(create_log_header) {
			fprintf(log_file_fp, "Date,Timestamp,%s\n", log_file_header);
			create_log_header = false;
		}
		fprintf(log_file_fp, "%s,%s,%s\n", date_buffer, time_buffer, log_file_data);
		fclose(log_file_fp);
		if(log_file_data) {
			free(log_file_data);
			log_file_data = NULL;
		}
	}
	loop_counter++;
	} while((loop_count == -1) || (loop_counter < loop_count));

	return ret;
}

int main(int argc, char **argv)
{
	esmi_status_t ret;
	/* Parse command arguments */
	ret = parsesmi_args(argc, argv);

	show_smi_end_message();
	/* Program termination */
	esmi_exit();

	if(log_file_header) {
		free(log_file_header);
		log_file_header = NULL;
	}
	if(log_file_name) {
		free(log_file_name);
		log_file_name = NULL;
	}
	if(stoploop_file_name) {
		free(stoploop_file_name);
		stoploop_file_name = NULL;
	}

	if (features)
		free(features);
	return ret;
}
