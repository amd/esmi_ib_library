/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright (c) 2020-22, Advanced Micro Devices, Inc.
 * All rights reserved.
 *
 * Developed by:
 *
 *                 AMD Research and AMD Software Development
 *
 *                 Advanced Micro Devices, Inc.
 *
 *                 www.amd.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal with the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimers.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimers in
 *    the documentation and/or other materials provided with the distribution.
 *  - Neither the names of <Name of Development Group, Name of Institution>,
 *    nor the names of its contributors may be used to endorse or promote
 *    products derived from this Software without specific prior written
 *    permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS WITH THE SOFTWARE.
 *
 */

#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <e_smi/e_smi.h>

#define RED "\x1b[31m"
#define MAG "\x1b[35m"
#define RESET "\x1b[0m"

#define ARGS_MAX 64
#define SHOWLINESZ 256

/* To handle multiple errors while reporting summary */
#define ESMI_ERROR	-1

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

static void print_socket_footer()
{
	int i;

	printf("\n----------------------------------");
	for (i = 0; i < sys_info.sockets; i++) {
		printf("-------------------");
	}
}

static void print_socket_header()
{
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
	int i;

	printf("\n");
	for (i = 1; i < 32; i++) {
		if (err_bits & (1 << i))
			printf(RED "Err[%d]: %s\n" RESET, i, esmi_get_err_msg(i));
	}
}

static esmi_status_t epyc_get_coreenergy(uint32_t core_id)
{
	esmi_status_t ret;
	uint64_t core_input = 0;

	ret = esmi_core_energy_get(core_id, &core_input);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get core[%d] energy, Err[%d]: %s\n",
			core_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("-------------------------------------------------");
	printf("\n| core[%03d] energy  | %17.3lf Joules \t|\n",
		core_id, (double)core_input/1000000);
	printf("-------------------------------------------------\n");

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_get_sockenergy(void)
{
	esmi_status_t ret;
	uint32_t i;
	uint64_t pkg_input = 0;
	uint32_t err_bits = 0;

	print_socket_header();
	printf("\n| Energy (K Joules)\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		ret = esmi_socket_energy_get(i, &pkg_input);
		if (!ret) {
			printf(" %-17.3lf|", (double)pkg_input/1000000000);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}
	print_socket_footer();
	printf("\n");
	err_bits_print(err_bits);

	if (err_bits > 1)
		return ESMI_ERROR;

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

	printf("\n| DDR Bandwidth\t\t\t |");
	snprintf(max_str, SHOWLINESZ, "\n| \tDDR Max BW (GB/s)\t |");
	snprintf(bw_str, SHOWLINESZ, "\n| \tDDR Utilized BW (GB/s)\t |");
	snprintf(pct_str, SHOWLINESZ, "\n| \tDDR Utilized Percent(%%)\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		printf("                  |");
		ret = esmi_ddr_bw_get(&ddr);
		bw_len = strlen(bw_str);
		pct_len = strlen(pct_str);
		max_len = strlen(max_str);
		if(!ret) {
			snprintf(max_str + max_len, SHOWLINESZ - max_len, " %-17d|", ddr.max_bw);
			snprintf(bw_str + bw_len, SHOWLINESZ - bw_len, " %-17d|", ddr.utilized_bw);
			snprintf(pct_str + pct_len, SHOWLINESZ - pct_len, " %-17d|", ddr.utilized_pct);
		} else {
			*err_bits |= 1 << ret;
			snprintf(max_str + max_len, SHOWLINESZ - max_len, " NA (Err: %-2d)     |", ret);
			snprintf(bw_str + bw_len, SHOWLINESZ - bw_len, " NA (Err: %-2d)     |", ret);
			snprintf(pct_str + pct_len, SHOWLINESZ - pct_len, " NA (Err: %-2d)     |", ret);
		}
	}
	printf("%s", max_str);
	printf("%s", bw_str);
	printf("%s", pct_str);
}

static esmi_status_t epyc_get_ddr_bw(void)
{
	esmi_status_t ret;
	uint32_t i;
	struct ddr_bw_metrics ddr;
	char bw_str[SHOWLINESZ] = {};
	char pct_str[SHOWLINESZ] = {};
	uint32_t bw_len;
	uint32_t pct_len;
	uint32_t err_bits = 0;

	print_socket_header();
	ddr_bw_get(&err_bits);

	print_socket_footer();
	err_bits_print(err_bits);
	if (err_bits > 1)
		return ESMI_ERROR;

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_get_temperature(void)
{
	esmi_status_t ret;
	uint32_t i;
	uint32_t tmon = 0;
	uint32_t err_bits = 0;

	print_socket_header();
	printf("\n| Temperature\t\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		ret = esmi_socket_temperature_get(i, &tmon);
		if (!ret) {
			printf(" %3.3f°C\t    |", (double)tmon/1000);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}
	print_socket_footer();
	err_bits_print(err_bits);
	if (err_bits > 1)
		return ESMI_ERROR;
	return ESMI_SUCCESS;
}

static esmi_status_t epyc_get_smu_fw_version(void)
{
	struct smu_fw_version smu_fw;
	esmi_status_t ret;
	uint32_t fclk, mclk;

	ret = esmi_smu_fw_version_get(&smu_fw);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get SMU Firmware Version, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("\n------------------------------------------");
	printf("\n| SMU FW Version   |  %u.%u.%u \t\t |\n",
		smu_fw.major, smu_fw.minor, smu_fw.debug);
	printf("------------------------------------------\n");

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
	printf("\n---------------------------------");
	printf("\n| HSMP Protocol Version  | %u\t|\n", hsmp_proto_ver);
	printf("---------------------------------\n");

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_get_prochot_status(void)
{
	esmi_status_t ret;
	uint32_t i;
	uint32_t prochot;
	uint32_t err_bits = 0;

	print_socket_header();

	printf("\n| ProchotStatus:\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		ret = esmi_prochot_status_get(i, &prochot);
		if (!ret) {
			printf(" %-17s|", prochot? "active" : "inactive");
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}
	print_socket_footer();
	printf("\n");
	err_bits_print(err_bits);
	if (err_bits > 1)
		return ESMI_ERROR;
	return ESMI_SUCCESS;
}
static bool print_src = false;

static void display_freq_limit_src_names(char **freq_src)
{
	int j = 0;
	int i;

	for (i = 0; i < sys_info.sockets; i++) {
		j = 0;
		printf("*%d Frequency limit source names: \n", i);
		while (freq_src[j + (i * ARRAY_SIZE(freqlimitsrcnames))]) {
			printf(" %s\n", freq_src[j +(i * ARRAY_SIZE(freqlimitsrcnames))]);
			j++;
		}
		if (j == 0)
			printf(" %s\n", "Reserved");
		printf("\n");
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
	uint8_t index;
	int size = ARRAY_SIZE(freqlimitsrcnames);

	printf("\n| Current Active Freq limit\t |");
	snprintf(str1, SHOWLINESZ, "\n| \t Freq limit (MHz) \t |");
	snprintf(str2, SHOWLINESZ, "\n| \t Freq limit source \t |");

	for (i = 0; i < sys_info.sockets; i++) {
		len1 = strlen(str1);
		len2 = strlen(str2);
		printf("                  |");
		ret = esmi_socket_current_active_freq_limit_get(i, &limit, freq_src + (i * size));
		if (!ret) {
			snprintf(str1 + len1, SHOWLINESZ - len1, " %-17u|", limit);
			snprintf(str2 + len2, SHOWLINESZ - len2, " Refer below[*%d]  |", i);
		} else {
			*err_bits |= 1 << ret;
			snprintf(str1 + len1, SHOWLINESZ - len1, " NA (Err: %-2d)     |", ret);
			snprintf(str2 + len2, SHOWLINESZ - len2, " NA (Err: %-2d)     |", ret);
		}
	}
	printf("%s", str1);
	printf("%s", str2);
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

	printf("\n| Socket frequency range\t |");
	snprintf(str1, SHOWLINESZ, "\n| \t Fmax (MHz)\t\t |");
	snprintf(str2, SHOWLINESZ, "\n| \t Fmin (MHz)\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		printf("                  |");
		len1 = strlen(str1);
		len2 = strlen(str2);
		ret = esmi_socket_freq_range_get(i, &fmax, &fmin);
		if (!ret) {
			snprintf(str1 + len1, SHOWLINESZ -len1, " %-17u|", fmax);
			snprintf(str2 + len2, SHOWLINESZ - len2 , " %-17u|", fmin);
		} else {
			*err_bits |= 1 << ret;
			snprintf(str1 + len1, SHOWLINESZ -len1, " NA (Err: %-2d)     |", ret);
			snprintf(str2 + len2, SHOWLINESZ - len2, " NA (Err: %-2d)     |", ret);
		}
	}
	printf("%s", str1);
	printf("%s", str2);
}

static esmi_status_t epyc_get_clock_freq(void)
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
	printf("\n| fclk (Mhz)\t\t\t |");
	snprintf(str, SHOWLINESZ, "\n| mclk (Mhz)\t\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		len = strlen(str);
		ret = esmi_fclk_mclk_get(i, &fclk, &mclk);
		if (!ret) {
			printf(" %-17d|", fclk);
			snprintf(str + len, SHOWLINESZ - len, " %-17d|", mclk);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
			snprintf(str + len, SHOWLINESZ - len, " NA (Err: %-2d)     |", ret);
		}
	}
	printf("%s", str);

	printf("\n| cclk (Mhz)\t\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		ret = esmi_cclk_limit_get(i, &cclk);
		if (!ret) {
			printf(" %-17d|", cclk);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}
	if (sys_info.show_addon_clock_metrics)
		sys_info.show_addon_clock_metrics(&err_bits, freq_src);

	print_socket_footer();
	printf("\n");
	err_bits_print(err_bits);
	if (print_src)
		display_freq_limit_src_names(freq_src);
	if (err_bits > 1)
		return ESMI_ERROR;

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

/* PCIe link frequency(LCLK) in MHz for different dpm level values */
static uint32_t lclk_freq[] = {300, 400, 593, 770};

static esmi_status_t epyc_set_lclk_dpm_level(uint8_t sock_id, uint8_t nbio_id, uint8_t min, uint8_t max)
{
	esmi_status_t ret;

	ret = esmi_socket_lclk_dpm_level_set(sock_id, nbio_id, min, max);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to set lclk dpm level for socket[%d], nbiod[%d], Err[%d]: %s\n",
			sock_id, nbio_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("Socket[%d] nbio[%d] LCLK frequency set to %u-%u MHz range successfully\n",
		sock_id, nbio_id, lclk_freq[min], lclk_freq[max]);

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_get_lclk_dpm_level(uint32_t sock_id, uint8_t nbio_id)
{
	struct dpm_level nbio;
	esmi_status_t ret;

	ret = esmi_socket_lclk_dpm_level_get(sock_id, nbio_id, &nbio);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get LCLK dpm level for socket[%d], nbiod[%d], "
		       "Err[%d]: %s\n",
			sock_id, nbio_id, ret, esmi_get_err_msg(ret));
		return ret;
	}

	printf("\n------------------------------------\n");
	printf("| \tMIN\t | %5u\t   |\n", nbio.min_dpm_level);
	printf("| \tMAX\t | %5u\t   |\n", nbio.max_dpm_level);
	printf("------------------------------------\n");

	return ret;
}

static esmi_status_t epyc_get_socketpower(void)
{
	esmi_status_t ret;
	uint32_t i;
	uint32_t power = 0, powerlimit = 0, powermax = 0;
	uint32_t err_bits = 0;

	print_socket_header();
	printf("\n| Power (Watts)\t\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		ret = esmi_socket_power_get(i, &power);
		if (!ret) {
			printf(" %-17.3f|", (double)power/1000);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}

	printf("\n| PowerLimit (Watts)\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		ret = esmi_socket_power_cap_get(i, &powerlimit);
		if (!ret) {
			printf(" %-17.3f|", (double)powerlimit/1000);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}

	printf("\n| PowerLimitMax (Watts)\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		ret = esmi_socket_power_cap_max_get(i, &powermax);
		if (!ret) {
			printf(" %-17.3f|", (double)powermax/1000);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}
	print_socket_footer();
	printf("\n");
	err_bits_print(err_bits);
	if (err_bits > 1)
		return ESMI_ERROR;
	return ESMI_SUCCESS;
}

static esmi_status_t epyc_get_coreperf(uint32_t core_id)
{
	esmi_status_t ret;
	uint32_t boostlimit = 0;
	/* Get the boostlimit value for a given core */
	ret = esmi_core_boostlimit_get(core_id, &boostlimit);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to get core[%d] boostlimit, Err[%d]: %s\n",
			core_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("--------------------------------------------------\n");
	printf("| core[%03d] boostlimit (MHz)\t | %-10u \t |\n", core_id, boostlimit);
	printf("--------------------------------------------------\n");

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
	if (blimit < boostlimit)
		printf("Core[%d] boostlimit set to max boost limit: %u MHz\n", core_id, blimit);
	else if (blimit > boostlimit)
		printf("Core[%d] boostlimit set to min boost limit: %u MHz\n", core_id, blimit);
	else
		printf("Core[%d] boostlimit set to %u MHz successfully\n", core_id, blimit);

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

	ret = esmi_socket_c0_residency_get(sock_id, &residency);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to get socket[%d] residency, Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("--------------------------------------\n");
	printf("| socket[%02d] c0_residency   | %2u %%   |\n", sock_id, residency);
	printf("--------------------------------------\n");

	return ESMI_SUCCESS;
}

static esmi_status_t epyc_get_dimm_temp_range_refresh_rate(uint8_t sock_id, uint8_t dimm_addr)
{
	struct temp_range_refresh_rate rate;
	esmi_status_t ret;

	ret = esmi_dimm_temp_range_and_refresh_rate_get(sock_id, dimm_addr, &rate);
	if (ret) {
		printf("Failed to get socket[%u] DIMM temperature range and refresh rate,"
			" Err[%d]: %s\n", sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}

	printf("---------------------------------------");
	printf("\n| Temp Range\t\t |");
	printf(" %-10u |", rate.range);
	printf("\n| Refresh rate\t\t |");
	printf(" %-10u |", rate.ref_rate);
	printf("\n---------------------------------------\n");

	return ret;
}

static esmi_status_t epyc_get_dimm_power(uint8_t sock_id, uint8_t dimm_addr)
{
	esmi_status_t ret;
	struct dimm_power d_power;

	ret = esmi_dimm_power_consumption_get(sock_id, dimm_addr, &d_power);
	if (ret) {
		printf("Failed to get socket[%u] DIMM power and update rate, Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}

	printf("---------------------------------------");
	printf("\n| Power(mWatts)\t\t |");
	printf(" %-10u |", d_power.power);
	printf("\n| Power update rate(ms)\t |");
	printf(" %-10u |", d_power.update_rate);
	printf("\n| Dimm address \t\t |");
	printf(" 0x%-8x |", d_power.dimm_addr);
	printf("\n---------------------------------------\n");

	return ret;
}

static esmi_status_t epyc_get_dimm_thermal(uint8_t sock_id, uint8_t dimm_addr)
{
	struct dimm_thermal d_sensor;
	esmi_status_t ret;
	float temp;

	ret = esmi_dimm_thermal_sensor_get(sock_id, dimm_addr, &d_sensor);
	if (ret) {
		printf("Failed to get socket[%u] DIMM temperature and update rate,"
			" Err[%d]: %s\n", sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("------------------------------------------");
	printf("\n| Temperature(°C)\t |");
	printf(" %-10.3f\t |", d_sensor.temp);
	printf("\n| Update rate(ms)\t |");
	printf(" %-10u\t |", d_sensor.update_rate);
	printf("\n| Dimm address returned\t |");
	printf(" 0x%-8x\t |", d_sensor.dimm_addr);
	printf("\n------------------------------------------\n");

	return ret;
}

static esmi_status_t epyc_get_core_clock(uint32_t core_id)
{
	esmi_status_t ret;
	uint32_t cclk;

	ret = esmi_current_freq_limit_core_get(core_id, &cclk);
	if (ret) {
		printf("Failed to get cclk value for core[%3u], Err[%d]: %s\n",
			core_id, ret, esmi_get_err_msg(ret));
		return ret;
	}

	printf("-----------------------------------------");
	printf("\n| CPU[%03u] core clock (MHz) : %u\t|\n", core_id, cclk);
	printf("-----------------------------------------\n");
	return ret;
}

static esmi_status_t epyc_get_power_telemetry()
{
	esmi_status_t ret;
	uint32_t power;
	int i;
	uint32_t err_bits = 0;

	print_socket_header();
	printf("\n| SVI Power Telemetry (mWatts) \t |");
	for (i = 0; i < sys_info.sockets; i++) {
		ret = esmi_pwr_svi_telemetry_all_rails_get(i, &power);
		if(!ret) {
			printf(" %-17.3f|", (double)power/1000);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}

	print_socket_footer();
	err_bits_print(err_bits);
	if (err_bits > 1)
		return ESMI_ERROR;

	return ESMI_SUCCESS;
}

static char *links[] = {"P0", "P1", "P2", "P3", "G0", "G1", "G2", "G3"};

const char *bw_type_list[3] = {"AGG_BW", "RD_BW", "WR_BW"};

static void find_link_bwtype_index(char *link, char *bw_type, int *link_ind, int *bw_type_ind)
{
	int i;

	for(i = 0; i < ARRAY_SIZE(links); i++) {
		if(!strcmp(link, links[i])) {
			*link_ind = i;
			break;
		}
	}
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
	int index = -1;
	struct link_id_bw_type io_link;

	find_link_bwtype_index(link, NULL, &index, NULL);
	if (index == -1) {
		printf("Please provide valid link name.\n");
		printf(MAG "Try --help for more information.\n" RESET);
		return ESMI_ERROR;
	}
	io_link.link_id =  1 << index;
	/* Aggregate bw = 1 */
	io_link.bw_type = 1 ;
	ret = esmi_current_io_bandwidth_get(sock_id, io_link, &bw);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get io bandwidth width for socket[%u] Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}

	printf("\n-----------------------------------------------------------\n");
	printf("| Current IO Aggregate bandwidth of link %s | %6u Mbps |\n", link, bw);
	printf("-----------------------------------------------------------\n");

	return ret;
}

static esmi_status_t epyc_get_xgmi_bandwidth_info(char *link, char *bw_type)
{
	struct link_id_bw_type xgmi_link;
	esmi_status_t ret;
	uint32_t bw;
	int id;
	int link_ind = -1, bw_ind = -1;

	find_link_bwtype_index(link, bw_type, &link_ind, &bw_ind);
	if (link_ind == -1 || bw_ind == -1) {
		printf("Please provide valid link name.\n");
		printf(MAG "Try --help for more information.\n" RESET);
		return ESMI_ERROR;
	}

	xgmi_link.link_id = 1 << link_ind;
	xgmi_link.bw_type = 1 << bw_ind;
	ret = esmi_current_xgmi_bw_get(xgmi_link, &bw);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get xgmi bandwidth width, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}

	printf("\n-------------------------------------------------------------\n");
	printf("| Current Aggregate bandwidth of xGMI link %s | %6u Mbps |\n", link, bw);
	printf("-------------------------------------------------------------\n");

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

static char *mode_strings[] = {
	"High performance mode",
	"Power efficiency mode",
	"IO performance mode"
};

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
	switch (mode) {
		case 1:
			printf("\nThis is a high performance mode,This mode favours core"
			       " performance.\nDefault df pstate and DLWM algorithms are"
			       " active in this mode\n");
			break;
		case 2:
			printf("\nCaution : This is a power efficiency mode,"
			       "\nSetting the power efficiency mode inturn impacts DF pstate,\n"
			       "core boost limits, core performance etc internally\n");
			break;
		case 4:
			printf("\nThis is a IO performance mode, This mode can result in"
			       "\nlower core performance in some case, to increase the IO throughput\n");
			break;
	}

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

static char *width_string[] = {
	"quarter",
	"half",
	"full"
};

static esmi_status_t epyc_set_gmi3_link_width(uint8_t sock_id, uint8_t min, uint8_t max)
{
	esmi_status_t ret;

	ret = esmi_gmi3_link_width_range_set(sock_id, min, max);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to set gmi3 link width for socket[%u] Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("Gmi3 link width is set to %s to %s width range successfully\n",
	       width_string[min], width_string[max]);

	return ret;
}

static void show_smi_message(void)
{
	printf("\n====================== EPYC System Management Interface ======================\n\n");
}

static void show_smi_end_message(void)
{
	printf("\n============================= End of EPYC SMI Log ============================\n");
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

static void socket_ver5_metrics(uint32_t *err_bits, char **freq_src)
{
	ddr_bw_get(err_bits);
	get_sock_freq_limit(err_bits, freq_src);
	get_sock_freq_range(err_bits);
}

static esmi_status_t show_socket_metrics(uint32_t *err_bits, char **freq_src)
{
	esmi_status_t ret;
	uint32_t i;
	uint64_t pkg_input = 0;
	uint32_t power = 0, powerlimit = 0, powermax = 0;
	uint32_t c0resi;

	print_socket_header();
	printf("\n| Energy (K Joules)\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		ret = esmi_socket_energy_get(i, &pkg_input);
		if (!ret) {
			printf(" %-17.3lf|", (double)pkg_input/1000000000);
		} else {
			*err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}

	printf("\n| Power (Watts)\t\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		ret = esmi_socket_power_get(i, &power);
		if (!ret) {
			printf(" %-17.3f|", (double)power/1000);
		} else {
			*err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}

	printf("\n| PowerLimit (Watts)\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		ret = esmi_socket_power_cap_get(i, &powerlimit);
		if (!ret) {
			printf(" %-17.3f|", (double)powerlimit/1000);
		} else {
			*err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}

	printf("\n| PowerLimitMax (Watts)\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		ret = esmi_socket_power_cap_max_get(i, &powermax);
		if(!ret) {
			printf(" %-17.3f|", (double)powermax/1000);
		} else {
			*err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}

	printf("\n| C0 Residency (%%)\t\t |");
	for (i = 0; i < sys_info.sockets; i++) {
		ret = esmi_socket_c0_residency_get(i, &c0resi);
		if(!ret) {
			printf(" %-17u|", c0resi);
		} else {
			*err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}
	/* proto version specific socket metrics are printed here */
	if (sys_info.show_addon_socket_metrics)
		sys_info.show_addon_socket_metrics(err_bits, freq_src);

	print_socket_footer();
	if (*err_bits > 1) {
		return ESMI_ERROR;
	}
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

static esmi_status_t show_cpu_energy_all(void)
{
	int i;
	uint64_t *input;
	uint32_t cpus, threads;
	esmi_status_t ret;

	cpus = sys_info.cpus/sys_info.threads_per_core;

	input = (uint64_t *) calloc(cpus, sizeof(uint64_t));
	if (NULL == input) {
		printf("Memory allocation failed all energy entries\n");
		return ESMI_ERROR;
	}

	ret = esmi_all_energies_get(input);
	if (ret != ESMI_SUCCESS) {
		printf("\nFailed: to get CPU energies, Err[%d]: %s",
			ret, esmi_get_err_msg(ret));
		free(input);
		return ret;
	}
	printf("\n| CPU energies in Joules:\t\t\t\t\t\t\t\t\t\t\t|");
	for (i = 0; i < cpus; i++) {
		if(!(i % 8)) {
			printf("\n| cpu [%3d] :", i);
		}
		printf(" %10.3lf", (double)input[i]/1000000);
		if (i % 8 == 7)
			printf("\t\t|");
	}
	free(input);

	return ESMI_SUCCESS;
}

static esmi_status_t show_cpu_boostlimit_all(void)
{
	int i;
	uint32_t boostlimit;
	uint32_t cpus, threads;
	esmi_status_t ret;

	cpus = sys_info.cpus/sys_info.threads_per_core;

	printf("\n| CPU boostlimit in MHz:\t\t\t\t\t\t\t\t\t\t\t|");
	for (i = 0; i < cpus; i++) {
		boostlimit = 0;
		ret = esmi_core_boostlimit_get(i, &boostlimit);
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

	return ESMI_SUCCESS;
}

static esmi_status_t show_core_clocks_all()
{
	esmi_status_t ret;
	uint32_t cpus, threads;
	uint32_t cclk;
	int i;

	cpus = sys_info.cpus/sys_info.threads_per_core;

	printf("\n| CPU core clock in MHz:\t\t\t\t\t\t\t\t\t\t\t|");
	for (i = 0; i < cpus; i++) {
		cclk = 0;
		ret = esmi_current_freq_limit_core_get(i, &cclk);
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
	return ESMI_SUCCESS;
}

static void cpu_ver5_metrics(uint32_t *err_bits)
{
	esmi_status_t ret;

	printf("\n--------------------------------------------------------------------"
		"---------------------------------------------");
	ret = show_core_clocks_all();
	*err_bits |= 1 << ret;
	printf("\n--------------------------------------------------------------------"
		"---------------------------------------------");
}

static esmi_status_t show_cpu_metrics(uint32_t *err_bits)
{
	esmi_status_t ret;
	uint32_t i, core_id;
	uint64_t core_input = 0;
	uint32_t boostlimit = 0;

	printf("\n\n--------------------------------------------------------------------"
		"---------------------------------------------");
	ret = show_cpu_energy_all();
	*err_bits |= 1 << ret;

	printf("\n--------------------------------------------------------------------"
		"---------------------------------------------\n");

	printf("\n--------------------------------------------------------------------"
		"---------------------------------------------");
	ret = show_cpu_boostlimit_all();
	*err_bits |= 1 << ret;

	printf("\n--------------------------------------------------------------------"
		"---------------------------------------------\n");

	/* proto version specific cpu metrics are printed here */
	if (sys_info.show_addon_cpu_metrics)
		sys_info.show_addon_cpu_metrics(err_bits);

	if (*err_bits > 1)
		return ESMI_ERROR;

	return ESMI_SUCCESS;
}

static esmi_status_t show_smi_all_parameters(void)
{
	char *freq_src[ARRAY_SIZE(freqlimitsrcnames) * sys_info.sockets];
	esmi_status_t ret;
	int i;
	uint32_t err_bits = 0;

	for (i = 0; i < (ARRAY_SIZE(freqlimitsrcnames) * sys_info.sockets); i++)
		freq_src[i] = NULL;

	show_system_info();

	show_socket_metrics(&err_bits, freq_src);

	show_cpu_metrics(&err_bits);

	printf("\n");
	if (print_src)
		display_freq_limit_src_names(freq_src);
	err_bits_print(err_bits);
	if (err_bits > 1)
		return ESMI_ERROR;

	return ESMI_SUCCESS;
}

static char* const feat_comm[] = {
	"Output Option<s>:",
	"  -h, --help\t\t\t\t\t\tShow this help message",
	"  -A, --showall\t\t\t\t\t\tGet all esmi parameter Values\n",
};

static char* const feat_energy[] = {
	"Get Option<s>:",
	"  --showcoreenergy [CORE]\t\t\t\tGet energy for a given CPU (Joules)",
	"  --showsockenergy\t\t\t\t\tGet energy for all sockets (KJoules)",
};

static char* const feat_ver2_get[] = {
	"  --showsockpower\t\t\t\t\tGet power metrics for all sockets (mWatts)",
	"  --showcorebl [CORE]\t\t\t\t\tGet Boostlimit for a given CPU (MHz)",
	"  --showsockc0res [SOCKET]\t\t\t\tGet c0_residency for a given socket (%%)",
	"  --showsmufwver\t\t\t\t\tShow SMU FW Version",
	"  --showhsmpprotover\t\t\t\t\tShow HSMP Protocol Version",
	"  --showprochotstatus\t\t\t\t\tShow HSMP PROCHOT status (in/active)",
	"  --showclocks\t\t\t\t\t\tShow (CPU, Mem & Fabric) clock frequencies (MHz)",
};

static char* const feat_ver2_set[] = {
	"Set Option<s>:",
	"  --setpowerlimit [SOCKET] [POWER]\t\t\tSet power limit"
	" for a given socket (mWatts)",
	"  --setcorebl [CORE] [BOOSTLIMIT]\t\t\tSet boost limit"
	" for a given core (MHz)",
	"  --setsockbl [SOCKET] [BOOSTLIMIT]\t\t\tSet Boost"
	" limit for a given Socket (MHz)",
	"  --apbdisable [SOCKET] [PSTATE]\t\t\tSet Data Fabric"
	" Pstate for a given socket, PSTATE = 0 to 3",
	"  --apbenable [SOCKET]\t\t\t\t\tEnable the Data Fabric performance"
	" boost algorithm for a given socket",
	"  --setxgmiwidth [MIN] [MAX]\t\t\t\tSet xgmi link width"
	" in a multi socket system, MIN = MAX = 0 to 2",
	"  --setlclkdpmlevel [SOCKET] [NBIOID] [MIN] [MAX]\tSet lclk dpm level"
	" for a given nbio, given socket "
	" \n\t\t\t\t\t\t\tMIN = MAX = 0-1 for v5, 0-3 for v4, NBIOID = 0-3 ",
};

static char* const feat_ver3[] = {
	"  --showddrbw\t\t\t\t\t\tShow DDR bandwidth details (Gbps)",
};

static char* const feat_ver4[] = {
	"  --showsockettemp\t\t\t\t\tShow Temperature monitor of socket (°C)",
};

static char* const feat_ver5_get[] = {
	"  --showdimmtemprange [SOCKET] [DIMM_ADDR]\t\tShow dimm temperature range and"
	" refresh rate",
	"  --showdimmthermal [SOCKET] [DIMM_ADDR]\t\tShow dimm thermal values",
	"  --showdimmpower [SOCKET] [DIMM_ADDR]\t\t\tShow dimm power consumption",
	"  --showcoreclock [CORE]\t\t\t\tShow core clock frequency (MHz) for a given core",
	"  --showsvipower \t\t\t\t\tShow svi based power telemetry of all rails",
	"  --showxgmibandwidth [LINKNAME] [BWTYPE]\t\tShow xGMI bandwidth for LINKNAME = P0-P3/G0-G3"
	" and BWTYPE = AGG_BW/RD_BW/WR_BW",
	"  --showiobandwidth [SOCKET] [LINKNAME]\t\t\tShow IO bandwidth for LINKNAME = P0-P3/G0-G3",
	"  --showlclkdpmlevel [SOCKET] [NBIOID]\t\t\tShow lclk dpm level"
	" for a given nbio, given socket"
};

static char* const feat_ver5_set[] = {
	"  --setpcielinkratecontrol [SOCKET] [CTL]\t\tSet rate control for pcie link"
	" for a given socket CTL = 0, 1, 2 ",
	"  --setpowerefficiencymode [SOCKET] [MODE]\t\tSet power efficiency mode"
	" for a given socket MODE = 0, 1, 2 ",
	"  --setdfpstaterange [SOCKET] [MAX] [MIN]\t\tSet df pstate range"
	" for a given socket MIN = MAX = 0 to 3 with MIN > MAX",
	"  --setgmi3linkwidth [SOCKET] [MIN] [MAX]\t\tSet gmi3 link width"
	" for a given socket MIN = MAX = 0 to 2 with MAX > MIN",
};

static char* const blankline[] = {""};

static char **features;

static void show_usage(char *exe_name)
{
	int i = 0;

	printf("Usage: %s [Option]... <INPUT>...\n\n", exe_name);
	while (features[i]) {
		printf("%s\n", features[i]);
		i++;
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
	memcpy(features + offset, blankline, sizeof(char *));
	offset += 1;
	memcpy(features + offset, feat_ver2_set, (ARRAY_SIZE(feat_ver2_set) * sizeof(char *)));
	offset += ARRAY_SIZE(feat_ver2_set);
	memcpy(features + offset, feat_ver5_set, (ARRAY_SIZE(feat_ver5_set) * sizeof(char *)));

	/* proto version 5 has extra socket metrics as well as extra cpu metrics */
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
		       ARRAY_SIZE(feat_ver2_get) + ARRAY_SIZE(feat_ver2_set);
		features = malloc((size + 1) * sizeof(char *));
		if (!features)
			return ESMI_NO_MEMORY;
		add_comm_and_energy_feat();
		add_hsmp_ver2_feat();
		break;
	case 4:
		size = ARRAY_SIZE(feat_comm) + ARRAY_SIZE(feat_ver2_get) +
		       ARRAY_SIZE(feat_ver2_set) + ARRAY_SIZE(feat_ver4) +
		       ARRAY_SIZE(feat_energy) + ARRAY_SIZE(feat_ver3);
		features = malloc((size + 1) * sizeof(char *));
		if (!features)
			return ESMI_NO_MEMORY;
		add_comm_and_energy_feat();
		add_hsmp_ver4_feat();
		break;
	case 5:
	default:
		size = ARRAY_SIZE(feat_comm) + ARRAY_SIZE(feat_ver2_get) +
		       ARRAY_SIZE(feat_ver2_set) + ARRAY_SIZE(feat_ver5_get) +
		       ARRAY_SIZE(feat_ver5_set) + ARRAY_SIZE(feat_ver3) +
		       ARRAY_SIZE(feat_energy);
		features = malloc((size + 1) * sizeof(char *));
		if (!features)
			return ESMI_NO_MEMORY;
		add_comm_and_energy_feat();
		add_hsmp_ver5_feat();
		break;
	}

	/* Indicate the array end with NULL pointer */
	features[size] = NULL;

	return ESMI_SUCCESS;
}

/**
Parse command line parameters and set data for program.
@param argc number of command line parameters
@param argv list of command line parameters
*/
static esmi_status_t parsesmi_args(int argc,char **argv)
{
	esmi_status_t ret = ESMI_ERROR;
	int i;
	int opt = 0; /* option character */
	uint32_t core_id = 0, sock_id = 0;
	uint32_t power = 0, boostlimit = 0;
	int32_t pstate;
	static char *args[ARGS_MAX];
	char sudostr[] = "sudo";
	uint8_t min, max;
	uint8_t nbio_id;
	uint8_t dimm_addr;
	char *end;
	char *link;
	char *link_name;
	char *bw_type;
	uint8_t ctrl, mode;

	//Specifying the expected options
	static struct option long_options[] = {
		{"help",                no_argument,		0,	'h'},
		{"showall",             no_argument,		0,	'A'},
		{"showcoreenergy",      required_argument,	0,	'e'},
		{"showsockenergy",	no_argument,		0,	's'},
		{"showsockpower",	no_argument,		0,	'p'},
		{"showsmufwver",	no_argument,		0,	'f'},
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
		{"showcoreclock",		required_argument,	0,	'q'},
		{"showsvipower",		no_argument,		0,	'm'},
		{"showiobandwidth",		required_argument,	0,	'B'},
		{"showxgmibandwidth",		required_argument,	0,	'i'},
		{"setpcielinkratecontrol",	required_argument,	0,	'j'},
		{"setpowerefficiencymode",	required_argument,	0,	'k'},
		{"setdfpstaterange",		required_argument,	0,	'X'},
		{"setgmi3linkwidth",		required_argument,	0,	'n'},
		{"showlclkdpmlevel",		required_argument,	0,	'Y'},
		{0,			0,			0,	0},
	};

	int long_index = 0;
	char *helperstring = "+hA";

	if (getuid() != 0) {
		while ((opt = getopt_long(argc, argv, helperstring,
				long_options, &long_index)) != -1) {
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
				case 'n':
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
	/* smi monitor objects initialization */
	ret = esmi_init();
	if(ret != ESMI_SUCCESS) {
		printf(RED "\tESMI Not initialized, drivers not found.\n"
		       "\tErr[%d]: %s\n" RESET, ret, esmi_get_err_msg(ret));
		return ESMI_ERROR;
	}

	ret = cache_system_info();
	if(ret != ESMI_SUCCESS) {
		printf(RED "\tError in reading system info.\n"
		       "\tErr[%d]: %s\n" RESET, ret, esmi_get_err_msg(ret));
		return ESMI_ERROR;
	}

	ret = init_proto_version_func_pointers();
	if (ret != ESMI_SUCCESS) {
		printf(RED "\tError in allocating memory \n"
		       "\tErr[%d]: %s\n" RESET, ret, esmi_get_err_msg(ret));
		return ESMI_ERROR;
	}

	if (argc <= 1) {
		ret = show_smi_all_parameters();
		printf(MAG"\nTry `%s --help' for more information." RESET
			"\n\n", argv[0]);
	}
	optind = 0;
	while ((opt = getopt_long(argc, argv, helperstring,
			long_options, &long_index)) != -1) {
	if (opt == 'e' ||
	    opt == 'L' ||
	    opt == 'C' ||
	    opt == 'a' ||
	    opt == 'b' ||
	    opt == 'c' ||
	    opt == 'y' ||
	    opt == 'u' ||
	    opt == 'w' ||
	    opt == 'l' ||
	    opt == 'H' ||
	    opt == 'T' ||
	    opt == 'g' ||
	    opt == 'q' ||
	    opt == 'B' ||
	    opt == 'j' ||
	    opt == 'k' ||
	    opt == 'X' ||
	    opt == 'n' ||
	    opt == 'Y' ||
	    opt == 'r') {
		if (is_string_number(optarg)) {
			printf("Option '-%c' require a valid numeric value"
					" as an argument\n\n", opt);
			show_usage(argv[0]);
			return ESMI_ERROR;
		}
	}
	if (opt == 'C' ||
	    opt == 'u' ||
	    opt == 'a' ||
	    opt == 'w' ||
	    opt == 'H' ||
	    opt == 'T' ||
	    opt == 'g' ||
	    opt == 'j' ||
	    opt == 'k' ||
	    opt == 'X' ||
	    opt == 'n' ||
	    opt == 'Y' ||
	    opt == 'b') {
		// make sure optind is valid  ... or another option
		if (optind >= argc) {
			printf(MAG "\nOption '-%c' require TWO arguments"
			 " <index>  <set_value>\n\n" RESET, opt);
			show_usage(argv[0]);
			return ESMI_ERROR;
		}
		if (opt != 'g' && opt != 'H' && opt != 'T') {
			if (*argv[optind] == '-') {
				if (*(argv[optind] + 1) < 48 && *(argv[optind] + 1) > 57) {
					printf(MAG "\nOption '-%c' require TWO arguments"
					 " <index>  <set_value>\n\n" RESET, opt);
					show_usage(argv[0]);
					return ESMI_ERROR;
				}
			}
			if (is_string_number(argv[optind])) {
				printf(MAG "Option '-%c' requires 2nd argument as valid"
				       " numeric value\n\n" RESET, opt);
				show_usage(argv[0]);
				return ESMI_ERROR;
			}
		} else {
			if (*argv[optind] == '-') {
				printf(MAG "\nOption '--%s' requires TWO arguments and value"
				       " should be non negative\n\n"
				       RESET, long_options[long_index].name);
				show_usage(argv[0]);
				return ESMI_ERROR;
			}
			if (!strncmp(argv[optind], "0x", 2) || !strncmp(argv[optind], "0X", 2)) {
				dimm_addr = strtoul(argv[optind++], &end, 16);
				if (*end) {
					printf(MAG "Option '--%s' requires 2nd argument as valid"
					       " numeric value\n\n"
					       RESET, long_options[long_index].name);
					show_usage(argv[0]);
					return ESMI_ERROR;
				}
			} else {
				if (is_string_number(argv[optind])) {
					printf(MAG "Option '--%s' requires 2nd argument as valid"
					       " numeric value\n\n"
					       RESET, long_options[long_index].name);
					show_usage(argv[0]);
					return ESMI_ERROR;
				}
				dimm_addr = atoi(argv[optind++]);
			}
		}
	}

	if (opt == 'l') {
		// make sure optind is valid  ... or another option
		if ((optind + 2) >= argc || *argv[optind] == '-'
		    || *argv[optind + 1] == '-' || *argv[optind + 2] == '-') {
			printf("\nOption '-%c' requires FOUR arguments"
			 " <socket> <nbioid> <min_value> <max_value>\n\n", opt);
			show_usage(argv[0]);
			return ESMI_ERROR;
		}

		if (is_string_number(argv[optind]) || is_string_number(argv[optind + 1])
		    || is_string_number(argv[optind + 2])) {
			printf("Option '-%c' requires 2nd, 3rd, 4th argument as valid"
					" numeric value\n\n", opt);
			show_usage(argv[0]);
			return ESMI_ERROR;
		}
	}

	if ((opt == 'B') || (opt == 'i'))
	{
		if ((optind >= argc) || (*optarg == '-') || (*argv[optind] == '-')) {
			printf("\nOption '-%c' requires two valid arguments"
			 " <arg1> <arg2>\n\n", opt);
			show_usage(argv[0]);
			return ESMI_ERROR;
		}
		if (opt == 'B') {
			if (is_string_number(optarg) || !is_string_number(argv[optind])) {
				printf("Please provide valid link names.\n");
				return ESMI_ERROR;
			}
		}
		if (opt == 'i') {
			if (!is_string_number(optarg) || !is_string_number(argv[optind])) {
				printf("Please provide valid link names.\n");
				return ESMI_ERROR;
			}
		}
	}

	if ((opt == 'X') || (opt == 'n')) {
		// make sure optind is valid  ... or another option
		if ((optind + 1) >= argc || *argv[optind] == '-'
		    || *argv[optind + 1] == '-') {
			printf("\nOption '-%c' requires THREE arguments"
			 " <socket> <min_value> <max_value>\n\n", opt);
			show_usage(argv[0]);
			return ESMI_ERROR;
		}

		if (is_string_number(argv[optind]) || is_string_number(argv[optind + 1])) {
			printf("Option '-%c' requires 2nd, 3rd, as valid"
					" numeric value\n\n", opt);
			show_usage(argv[0]);
			return ESMI_ERROR;
		}
	}

	switch (opt) {
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
			ret = epyc_get_dimm_power(sock_id, dimm_addr);
			break;
		case 'T' :
			/* Get DIMM temp range and refresh rate */
			sock_id = atoi(optarg);
			ret = epyc_get_dimm_temp_range_refresh_rate(sock_id, dimm_addr);
			break;
		case 'H' :
			/* Get DIMM temperature */
			sock_id = atoi(optarg);
			ret = epyc_get_dimm_thermal(sock_id, dimm_addr);
			break;
		case 'q' :
			/* Get the core clock value for a given core */
			core_id = atoi(optarg);
			ret = epyc_get_core_clock(core_id);
			break;
		case 'm' :
			/* Get svi based power telemetry of all rails */
			epyc_get_power_telemetry();
			break;
		case 'i' :
			/* Get xgmi bandiwdth info on specified link */
			bw_type = argv[optind++];
			link_name = optarg;
			epyc_get_xgmi_bandwidth_info(link_name, bw_type);
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
		case 'A' :
			ret = show_smi_all_parameters();
			break;
		case 'h' :
			show_usage(argv[0]);
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

	if (features)
		free(features);
	return ret;
}
