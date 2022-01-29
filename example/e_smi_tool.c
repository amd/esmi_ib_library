/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright (c) 2020, Advanced Micro Devices, Inc.
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
#define ESMI_MULTI_ERROR	1234

uint32_t err_bits;

esmi_status_t show_smi_parameters(void);
esmi_status_t show_smi_all_parameters(void);
void show_smi_message(void);
void show_smi_end_message(void);

void print_socket_footer(uint32_t sockets)
{
	int i;

	printf("\n--------------------------");
	for (i = 0; i < sockets; i++) {
		printf("-------------------");
	}
}

void print_socket_header(uint32_t sockets)
{
	int i;

	print_socket_footer(sockets);
	printf("\n| Sensor Name\t\t |");
	for (i = 0; i < sockets; i++) {
		printf(" Socket %d         |", i);
	}
	print_socket_footer(sockets);
}

void err_bits_reset(void)
{
	err_bits = 0;
}

void err_bits_print(void)
{
	int i;

	printf("\n");
	for (i = 1; i < 32; i++) {
		if (i == ESMI_MULTI_ERROR) {
			continue;
		}
		if (err_bits & (1 << i)) {
			printf(RED "Err[%d]: %s\n" RESET, i, esmi_get_err_msg(i));
		}
	}
}

esmi_status_t epyc_get_coreenergy(uint32_t core_id)
{
	esmi_status_t ret;
	uint64_t core_input = 0;

	ret = esmi_core_energy_get(core_id, &core_input);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get core[%d] energy, Err[%d]: %s\n",
			core_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("core[%d] energy\t: %17.3lf Joules\n",
		core_id, (double)core_input/1000000);

	return ESMI_SUCCESS;
}

esmi_status_t epyc_get_sockenergy(void)
{
	esmi_status_t ret;
	uint32_t i, sockets;
	uint64_t pkg_input = 0;

	ret = esmi_number_of_sockets_get(&sockets);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get number of sockets, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}

	err_bits_reset();
	print_socket_header(sockets);
	printf("\n| Energy (K Joules)\t |");
	for (i = 0; i < sockets; i++) {
		ret = esmi_socket_energy_get(i, &pkg_input);
		if (!ret) {
			printf(" %-17.3lf|", (double)pkg_input/1000000000);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}
	print_socket_footer(sockets);
	printf("\n");
	err_bits_print();
	if (err_bits > 1) {
		return ESMI_MULTI_ERROR;
	}
	return ESMI_SUCCESS;
}

esmi_status_t epyc_get_ddr_bw(void)
{
	esmi_status_t ret;
	uint32_t i, sockets;
	struct ddr_bw_metrics ddr;
	char bw_str[SHOWLINESZ] = {};
	char pct_str[SHOWLINESZ] = {};
	uint32_t bw_len;
	uint32_t pct_len;

	ret = esmi_number_of_sockets_get(&sockets);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get number of sockets, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	for (i = 0; i < sockets; i++) {
		ret = esmi_ddr_bw_get(&ddr);
		if (ret == ESMI_NO_HSMP_SUP) {
			printf("Get DDR Bandwidth info: Not supported.\n");
			return ret;
		}
		if (i == 0) {
			err_bits_reset();
			print_socket_header(sockets);
			printf("\n| DDR Max BW (GB/s)\t |");
			snprintf(bw_str, SHOWLINESZ, "\n| DDR Utilized BW (GB/s) |");
			snprintf(pct_str, SHOWLINESZ, "\n| DDR Utilized Percent(%%)|");
		}
		bw_len = strlen(bw_str);
		pct_len = strlen(pct_str);
		if(!ret) {
			printf(" %-17d|", ddr.max_bw);
			snprintf(bw_str + bw_len, SHOWLINESZ - bw_len, " %-17d|", ddr.utilized_bw);
			snprintf(pct_str + pct_len, SHOWLINESZ - pct_len, " %-17d|", ddr.utilized_pct);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
			snprintf(bw_str + bw_len, SHOWLINESZ - bw_len, " NA (Err: %-2d)     |", ret);
			snprintf(pct_str + pct_len, SHOWLINESZ - pct_len, " NA (Err: %-2d)     |", ret);
		}
	}
	printf("%s", bw_str);
	printf("%s", pct_str);

	print_socket_footer(sockets);
	err_bits_print();
	if (err_bits > 1) {
		return ESMI_MULTI_ERROR;
	}
	return ESMI_SUCCESS;
}

esmi_status_t epyc_get_temperature(void)
{
	esmi_status_t ret;
	uint32_t i, sockets;
	uint32_t tmon = 0;

	ret = esmi_number_of_sockets_get(&sockets);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get number of sockets, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}

	err_bits_reset();
	print_socket_header(sockets);
	printf("\n| Temperature\t\t |");
	for (i = 0; i < sockets; i++) {
		ret = esmi_socket_temperature_get(i, &tmon);
		if (ret == ESMI_NO_HSMP_SUP) {
			printf("Temperature Monitor Not supported.\n");
			return ret;
		}
		if (!ret) {
			printf(" %-15.3f°C|", (double)tmon/1000);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}
	print_socket_footer(sockets);
	err_bits_print();
	if (err_bits > 1) {
		return ESMI_MULTI_ERROR;
	}
	return ESMI_SUCCESS;
}

esmi_status_t epyc_get_smu_fw_version(void)
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
	printf("SMU FW Version: %u.%u.%u\n",
		smu_fw.major, smu_fw.minor, smu_fw.debug);

	return ESMI_SUCCESS;
}

esmi_status_t epyc_get_hsmp_proto_version(void)
{
	uint32_t hsmp_proto_ver;
	esmi_status_t ret;

	ret = esmi_hsmp_proto_ver_get(&hsmp_proto_ver);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get hsmp protocol version, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("HSMP Protocol Version: %u\n", hsmp_proto_ver);

	return ESMI_SUCCESS;
}

esmi_status_t epyc_get_prochot_status(void)
{
	esmi_status_t ret;
	uint32_t i, sockets;
	uint32_t prochot;

	ret = esmi_number_of_sockets_get(&sockets);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get number of sockets, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	err_bits_reset();
	print_socket_header(sockets);

	printf("\n| ProchotStatus:\t |");
	for (i = 0; i < sockets; i++) {
		ret = esmi_prochot_status_get(i, &prochot);
		if (!ret) {
			printf(" %-17s|", prochot? "active" : "inactive");
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}
	print_socket_footer(sockets);
	printf("\n");
	err_bits_print();
	if (err_bits > 1) {
		return ESMI_MULTI_ERROR;
	}
	return ESMI_SUCCESS;
}

#define MCLKSZ 256
esmi_status_t epyc_get_clock_freq(void)
{
	esmi_status_t ret;
	uint32_t i, sockets;
	uint32_t fclk, mclk, cclk;
	char str[MCLKSZ] = {};
	uint32_t len;

	ret = esmi_number_of_sockets_get(&sockets);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get number of sockets, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	err_bits_reset();
	print_socket_header(sockets);
	printf("\n| fclk (Mhz)\t\t |");
	snprintf(str, MCLKSZ, "\n| mclk (Mhz)\t\t |");
	for (i = 0; i < sockets; i++) {
		len = strlen(str);
		ret = esmi_fclk_mclk_get(i, &fclk, &mclk);
		if (!ret) {
			printf(" %-17d|", fclk);
			snprintf(str + len, MCLKSZ - len, " %-17d|", mclk);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
			snprintf(str + len, MCLKSZ - len, " NA (Err: %-2d)     |", ret);
		}
	}
	printf("%s", str);

	printf("\n| cclk (Mhz)\t\t |");
	for (i = 0; i < sockets; i++) {
		ret = esmi_cclk_limit_get(i, &cclk);
		if (!ret) {
			printf(" %-17d|", cclk);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}
	print_socket_footer(sockets);
	printf("\n");
	err_bits_print();
	if (err_bits > 1) {
		return ESMI_MULTI_ERROR;
	}
	return ESMI_SUCCESS;
}

esmi_status_t epyc_apb_enable(uint32_t sock_id)
{
	bool prochot_asserted;
	esmi_status_t ret;

	ret = esmi_apb_enable(sock_id, &prochot_asserted);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to enable DF performance boost algo on "
			"socket[%d], Err[%d]: %s\n", sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}

	if (prochot_asserted)
		printf("PROCHOT_L is asserted, lowest DF-Pstate is enforced.\n");
	else
		printf("Enabled performance boost algorithm on socket[%d] successfully\n",
			sock_id);

	return ESMI_SUCCESS;
}

esmi_status_t epyc_set_df_pstate(uint32_t sock_id, int32_t pstate)
{
	esmi_status_t ret;
	bool prochot_asserted;

	ret = esmi_apb_disable(sock_id, pstate, &prochot_asserted);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to set socket[%d] DF Pstate\n", sock_id);
		printf(RED "Err[%d]: %s\n" RESET, ret, esmi_get_err_msg(ret));
		return ret;
	}

	if (prochot_asserted)
		printf("PROCHOT_L is asserted, lowest DF-Pstate is enforced.\n");
	else
		printf("Socket[%d] P-state set to %d successfully\n",
			sock_id, pstate);

	return ESMI_SUCCESS;
}

/* 0, 1, 2 values correspond to 2, 8, 16 xgmi lanes respectively */
static uint8_t xgmi_links[] = {2, 8, 16};

esmi_status_t epyc_set_xgmi_width(uint8_t min, uint8_t max)
{
	esmi_status_t ret;

	ret = esmi_xgmi_width_set(min, max);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to set xGMI link width, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("xGMI link width set to %u-%u range successfully\n",
		xgmi_links[min], xgmi_links[max]);

	return ESMI_SUCCESS;
}

/* PCIe link frequency(LCLK) in MHz for different dpm level values */
static uint32_t lclk_freq[] = {300, 400, 593, 770};

esmi_status_t epyc_set_lclk_dpm_level(uint8_t sock_id, uint8_t nbio_id, uint8_t min, uint8_t max)
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

esmi_status_t epyc_get_socketpower(void)
{
	esmi_status_t ret;
	uint32_t i, sockets;
	uint32_t power = 0, powerlimit = 0, powermax = 0;

	ret = esmi_number_of_sockets_get(&sockets);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get number of sockets, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	err_bits_reset();
	print_socket_header(sockets);
	printf("\n| Power (Watts)\t\t |");
	for (i = 0; i < sockets; i++) {
		ret = esmi_socket_power_get(i, &power);
		if (!ret) {
			printf(" %-17.3f|", (double)power/1000);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}

	printf("\n| PowerLimit (Watts)\t |");
	for (i = 0; i < sockets; i++) {
		ret = esmi_socket_power_cap_get(i, &powerlimit);
		if (!ret) {
			printf(" %-17.3f|", (double)powerlimit/1000);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}

	printf("\n| PowerLimitMax (Watts)\t |");
	for (i = 0; i < sockets; i++) {
		ret = esmi_socket_power_cap_max_get(i, &powermax);
		if (!ret) {
			printf(" %-17.3f|", (double)powermax/1000);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}
	print_socket_footer(sockets);
	printf("\n");
	err_bits_print();
	if (err_bits > 1) {
		return ESMI_MULTI_ERROR;
	}
	return ESMI_SUCCESS;
}

esmi_status_t epyc_get_coreperf(uint32_t core_id)
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
	printf("core[%d] boostlimit\t: %u MHz\n", core_id, boostlimit);

	return ESMI_SUCCESS;
}

esmi_status_t epyc_setpowerlimit(uint32_t sock_id, uint32_t power)
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
	printf("Socket[%d] power_limit set to %15.03f Watts successfully\n",
		sock_id, (double)power/1000);

	return ESMI_SUCCESS;
}

esmi_status_t epyc_setcoreperf(uint32_t core_id, uint32_t boostlimit)
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
	if (ret == ESMI_SUCCESS) {
		if (blimit < boostlimit) {
			printf("Set to max boost limit: %u MHz\n", blimit);
		} else if (blimit > boostlimit) {
			printf("Set to min boost limit: %u MHz\n", blimit);
		}
	}
	printf("Core[%d] boostlimit set to %u MHz successfully\n", core_id, boostlimit);

	return ESMI_SUCCESS;
}

esmi_status_t epyc_setsocketperf(uint32_t sock_id, uint32_t boostlimit)
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
	if (ret == ESMI_SUCCESS) {
		if (blimit < boostlimit) {
			printf("Set to max boost limit: %u MHz\n", blimit);
		} else if (blimit > boostlimit) {
			printf("Set to min boost limit: %u MHz\n", blimit);
		}
	}
	printf("Socket[%d] boostlimit set to %u MHz successfully\n", sock_id, blimit);

	return ESMI_SUCCESS;
}

esmi_status_t epyc_setpkgperf(uint32_t boostlimit)
{
	esmi_status_t ret;
	uint32_t blimit = 0;

	ret = esmi_package_boostlimit_set(boostlimit);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to set package boostlimit to %u MHz, "
			"Err[%d]: %s\n", boostlimit, ret,
			esmi_get_err_msg(ret));
		return ret;
	}
	ret = esmi_core_boostlimit_get(0, &blimit);
	if (ret == ESMI_SUCCESS) {
		if (blimit < boostlimit) {
			printf("Set to max boost limit: %u MHz\n", blimit);
		} else if (blimit > boostlimit) {
			printf("Set to min boost limit: %u MHz\n", blimit);
		}
	}
	printf("Package boostlimit set to %u MHz successfully\n", blimit);

	return ESMI_SUCCESS;
}

esmi_status_t epyc_get_sockc0_residency(uint32_t sock_id)
{
	esmi_status_t ret;
	uint32_t residency = 0;

	ret = esmi_socket_c0_residency_get(sock_id, &residency);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to get socket[%d] residency, Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("socket[%d] c0_residency	: %u %%\n", sock_id, residency);

	return ESMI_SUCCESS;
}

static void show_usage(char *exe_name)
{
	printf("Usage: %s [Option]... <INPUT>...\n\n"
	"Output Option<s>:\n"
	"  -h, --help\t\t\t\t\t\tShow this help message\n"
	"  -A, --showall\t\t\t\t\t\tGet all esmi parameter Values\n"
	"\n"
	"Get Option<s>:\n"
	"  -e, --showcoreenergy [CORE]\t\t\t\tGet energy for a given"
	" CPU (Joules)\n"
	"  -s, --showsockenergy\t\t\t\t\tGet energy for all sockets (KJoules)\n"
	"  -p, --showsockpower\t\t\t\t\tGet power metrics for all sockets (mWatts)\n"
	"  -L, --showcorebl [CORE]\t\t\t\tGet Boostlimit for a"
	" given CPU (MHz)\n"
	"  -r, --showsockc0res [SOCKET]\t\t\t\tGet c0_residency for a"
	" given socket (%%)\n"
	"  -d, --showddrbw\t\t\t\t\tShow DDR bandwidth details (Gbps)\n"
	"  -t, --showsockettemp\t\t\t\t\tShow Temperature monitor of socket (°C)\n"
	"  --showsmufwver\t\t\t\t\tShow SMU FW Version\n"
	"  --showhsmpprotover\t\t\t\t\tShow HSMP Protocol Version\n"
	"  --showprochotstatus\t\t\t\t\tShow HSMP PROCHOT status (in/active)\n"
	"  --showclocks\t\t\t\t\t\tShow (CPU, Mem & Fabric) clock frequencies (MHz)\n"
	"\n"
	"Set Option<s>:\n"
	"  -C, --setpowerlimit [SOCKET] [POWER]\t\t\tSet power limit"
	" for a given socket (mWatts)\n"
	"  -a, --setcorebl [CORE] [BOOSTLIMIT]\t\t\tSet boost limit"
	" for a given core (MHz)\n"
	"  --setsockbl [SOCKET] [BOOSTLIMIT]\t\t\tSet Boost"
	" limit for a given Socket (MHz)\n"
	"  --setpkgbl [BOOSTLIMIT]\t\t\t\tSet Boost limit"
	" for all sockets in a package (MHz)\n"
	"  --apbdisable [SOCKET] [PSTATE]\t\t\tSet Data Fabric"
	" Pstate for a given socket, PSTATE = 0 to 3\n"
	"  --apbenable [SOCKET]\t\t\t\t\tEnable the Data Fabric performance"
	" boost algorithm for a given socket\n"
	"  --setxgmiwidth [MIN] [MAX]\t\t\t\tSet xgmi link width"
	" in a multi socket system, MIN = MAX = 0 to 2\n"
	"  --setlclkdpmlevel [SOCKET] [NBIOID] [MIN] [MAX]\tSet lclk dpm level"
	" for a given nbio, given socket, MIN = MAX = NBIOID = 0 to 3\n"
	, exe_name);
}

void show_smi_message(void)
{
	printf("\n=============== EPYC System Management Interface ===============\n\n");
}

void show_smi_end_message(void)
{
	printf("\n====================== End of EPYC SMI Log =====================\n");
}

esmi_status_t show_cpu_metrics(void)
{
	esmi_status_t ret;
	uint32_t i, core_id, sockets;
	uint64_t core_input = 0;
	uint32_t boostlimit = 0;

	ret = esmi_number_of_sockets_get(&sockets);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get number of sockets, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("\n| Core[0] Energy (Joules)|");
	for (i = 0; i < sockets; i++) {
		ret = esmi_first_online_core_on_socket(i, &core_id);
		if (ret) {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
		ret = esmi_core_energy_get(core_id, &core_input);
		if (!ret) {
			printf(" %-17.3lf|", (double)core_input/1000000);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}

	printf("\n| Core[0] boostlimit(MHz)|");
	for (i = 0; i < sockets; i++) {
		ret = esmi_first_online_core_on_socket(i, &core_id);
		if (ret) {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
		ret = esmi_core_boostlimit_get(core_id, &boostlimit);
		if (!ret) {
			printf(" %-17u|", boostlimit);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}
	print_socket_footer(sockets);
	if (err_bits > 1) {
		return ESMI_MULTI_ERROR;
	}
	return ESMI_SUCCESS;
}

esmi_status_t show_socket_metrics(void)
{
	esmi_status_t ret;
	uint32_t i, sockets;
	uint64_t pkg_input = 0;
	uint32_t power = 0, powerlimit = 0, powermax = 0;
	struct ddr_bw_metrics ddr;
	char bw_str[SHOWLINESZ] = {};
	char pct_str[SHOWLINESZ] = {};
	uint32_t bw_len;
	uint32_t pct_len;
	uint32_t c0resi;
	uint32_t tmon = 0;

	ret = esmi_number_of_sockets_get(&sockets);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get number of sockets, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}

	print_socket_header(sockets);
	printf("\n| Energy (K Joules)\t |");
	for (i = 0; i < sockets; i++) {
		ret = esmi_socket_energy_get(i, &pkg_input);
		if (!ret) {
			printf(" %-17.3lf|", (double)pkg_input/1000000000);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}

	printf("\n| Power (Watts)\t\t |");
	for (i = 0; i < sockets; i++) {
		ret = esmi_socket_power_get(i, &power);
		if (!ret) {
			printf(" %-17.3f|", (double)power/1000);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}

	printf("\n| PowerLimit (Watts)\t |");
	for (i = 0; i < sockets; i++) {
		ret = esmi_socket_power_cap_get(i, &powerlimit);
		if (!ret) {
			printf(" %-17.3f|", (double)powerlimit/1000);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}

	printf("\n| PowerLimitMax (Watts)\t |");
	for (i = 0; i < sockets; i++) {
		ret = esmi_socket_power_cap_max_get(i, &powermax);
		if(!ret) {
			printf(" %-17.3f|", (double)powermax/1000);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}

	printf("\n| C0 Residency (%%)\t |");
	for (i = 0; i < sockets; i++) {
		ret = esmi_socket_c0_residency_get(i, &c0resi);
		if(!ret) {
			printf(" %-17u|", c0resi);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}

	for (i = 0; i < sockets; i++) {
		ret = esmi_ddr_bw_get(&ddr);
		if (ret == ESMI_NO_HSMP_SUP) {
			break;
		}
		if (i == 0) {
			printf("\n| DDR Max BW (GB/s)\t |");
			snprintf(bw_str, SHOWLINESZ, "\n| DDR Utilized BW (GB/s) |");
			snprintf(pct_str, SHOWLINESZ, "\n| DDR Utilized Percent(%%)|");
		}
		bw_len = strlen(bw_str);
		pct_len = strlen(pct_str);
		if(!ret) {
			printf(" %-17d|", ddr.max_bw);
			snprintf(bw_str + bw_len, SHOWLINESZ - bw_len, " %-17d|", ddr.utilized_bw);
			snprintf(pct_str + pct_len, SHOWLINESZ - pct_len, " %-17d|", ddr.utilized_pct);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
			snprintf(bw_str + bw_len, SHOWLINESZ - bw_len, " NA (Err: %-2d)     |", ret);
			snprintf(pct_str + pct_len, SHOWLINESZ - pct_len, " NA (Err: %-2d)     |", ret);
		}
	}
	printf("%s", bw_str);
	printf("%s", pct_str);

	printf("\n| Temperature (°C)\t |");
	for (i = 0; i < sockets; i++) {
		ret = esmi_socket_temperature_get(i, &tmon);
		if (ret == ESMI_NO_HSMP_SUP) {
			break;
		}
		if (!ret) {
			printf(" %-17.3f|", (double)tmon/1000);
		} else {
			err_bits |= 1 << ret;
			printf(" NA (Err: %-2d)     |", ret);
		}
	}


	print_socket_footer(sockets);
	if (err_bits > 1) {
		return ESMI_MULTI_ERROR;
	}
	return ESMI_SUCCESS;
}

esmi_status_t show_cpu_details(void)
{
	esmi_status_t ret;
	uint32_t cpus, sockets, threads, family, model;

	ret = esmi_number_of_cpus_get(&cpus);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get number of cpus, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	ret = esmi_number_of_sockets_get(&sockets);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get number of sockets, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	ret = esmi_threads_per_core_get(&threads);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get threads per core, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	ret = esmi_cpu_family_get(&family);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get cpu family, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	ret = esmi_cpu_model_get(&model);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get cpu model, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}

	printf("--------------------------------------\n");
	printf("| CPU Family		| 0x%-2x (%-3d) |\n", family, family);
	printf("| CPU Model		| 0x%-2x (%-3d) |\n", model, model);
	printf("| NR_CPUS		| %-8d   |\n", cpus);
	printf("| NR_SOCKETS		| %-8d   |\n", sockets);
	if (threads > 1) {
		printf("| THREADS PER CORE	| %d (SMT ON) |\n", threads);
	} else {
		printf("| THREADS PER CORE	| %d (SMT OFF)|\n", threads);
	}
	printf("--------------------------------------\n");

	return ESMI_SUCCESS;
}

esmi_status_t show_smi_parameters(void)
{
	esmi_status_t ret;

	err_bits_reset();
	ret = show_cpu_details();
	err_bits |= 1 << ret;

	ret = show_socket_metrics();
	err_bits |= 1 << ret;

	ret = show_cpu_metrics();
	err_bits |= 1 << ret;

	printf("\n");
	err_bits_print();
	if (err_bits > 1) {
		return ESMI_MULTI_ERROR;
	}

	return ESMI_SUCCESS;
}

esmi_status_t show_cpu_energy_all(void)
{
	int i;
	uint64_t *input;
	uint32_t cpus, threads;
	esmi_status_t ret;

	ret = esmi_threads_per_core_get(&threads);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get threads per core, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	ret = esmi_number_of_cpus_get(&cpus);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get number of cpus, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	cpus = cpus/threads;

	input = (uint64_t *) calloc(cpus, sizeof(uint64_t));
	if (NULL == input) {
		printf("Memory allocation failed all energy entries\n");
		return ret;
	}

	ret = esmi_all_energies_get(input);
	if (ret != ESMI_SUCCESS) {
		printf("\nFailed: to get CPU energies, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		free(input);
		return ret;
	}
	printf("\nCPU energies in Joules:");
	for (i = 0; i < cpus; i++) {
		if(!(i % 8)) {
			printf("\ncpu [%3d] :", i);
		}
		printf(" %12.3lf", (double)input[i]/1000000);
	}
	free(input);

	return ESMI_SUCCESS;
}

esmi_status_t show_cpu_boostlimit_all(void)
{
	int i;
	uint32_t boostlimit;
	uint32_t cpus, threads;
	esmi_status_t ret;

	ret = esmi_threads_per_core_get(&threads);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get threads per core, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	ret = esmi_number_of_cpus_get(&cpus);
	if (ret != ESMI_SUCCESS) {
		printf("Failed to get number of cpus, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	cpus = cpus/threads;

	printf("\n\nCPU boostlimit in MHz:");
	for (i = 0; i < cpus; i++) {
		boostlimit = 0;
		ret = esmi_core_boostlimit_get(i, &boostlimit);
		if(!(i % 16)) {
			printf("\ncpu [%3d] :", i);
		}
		if (!ret) {
			printf(" %5u", boostlimit);
		} else {
			printf(" NA   ");
		}
	}

	return ESMI_SUCCESS;
}

esmi_status_t show_smi_all_parameters(void)
{
	esmi_status_t ret;

	err_bits_reset();

	ret = show_socket_metrics();
	err_bits |= 1 << ret;

	ret = show_cpu_energy_all();
	err_bits |= 1 << ret;

	ret = show_cpu_boostlimit_all();
	err_bits |= 1 << ret;

	printf("\n");
	err_bits_print();
	if (err_bits > 1) {
		return ESMI_MULTI_ERROR;
	}

	return ESMI_SUCCESS;
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

/**
Parse command line parameters and set data for program.
@param argc number of command line parameters
@param argv list of command line parameters
*/
esmi_status_t parsesmi_args(int argc,char **argv)
{
	esmi_status_t ret;
	int i;
	int opt = 0; /* option character */
	uint32_t core_id = 0, sock_id = 0;
	uint32_t power = 0, boostlimit = 0;
	int32_t pstate;
	static char *args[ARGS_MAX];
	char sudostr[] = "sudo";
	uint8_t min, max;
	uint8_t nbio_id;

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
		{"setpkgbl",		required_argument,	0,	'c'},
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
		{0,			0,			0,	0},
	};

	int long_index = 0;
	char *helperstring = "+hAsfpvxzdte:y:u:L:C:a:b:c:r:w:l:";

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
		return ESMI_MULTI_ERROR;
	}
	if (argc <= 1) {
		ret = show_smi_parameters();
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
	    opt == 'r') {
		if (is_string_number(optarg)) {
			printf("Option '-%c' require a valid numeric value"
					" as an argument\n\n", opt);
			show_usage(argv[0]);
			return ESMI_MULTI_ERROR;
		}
	}
	if (opt == 'C' ||
	    opt == 'u' ||
	    opt == 'a' ||
	    opt == 'w' ||
	    opt == 'b') {
		// make sure optind is valid  ... or another option
		if (optind >= argc) {
			printf(MAG "\nOption '-%c' require TWO arguments"
			 " <index>  <set_value>\n\n" RESET, opt);
			show_usage(argv[0]);
			return ESMI_MULTI_ERROR;
		}
		if (*argv[optind] == '-') {
			if (*(argv[optind] + 1) < 48 && *(argv[optind] + 1) > 57) {
				printf(MAG "\nOption '-%c' require TWO arguments"
				 " <index>  <set_value>\n\n" RESET, opt);
				show_usage(argv[0]);
				return ESMI_MULTI_ERROR;
			}
		}
		if(is_string_number(argv[optind])) {
			printf(MAG "Option '-%c' require 2nd argument as valid"
					" numeric value\n\n" RESET, opt);
			show_usage(argv[0]);
			return ESMI_MULTI_ERROR;
		}
	}

	if (opt == 'l') {
		// make sure optind is valid  ... or another option
		if ((optind + 2) >= argc || *argv[optind] == '-'
		    || *argv[optind + 1] == '-' || *argv[optind + 2] == '-') {
			printf("\nOption '-%c' requires FOUR arguments"
			 " <socket> <nbioid> <min_value> <max_value>\n\n", opt);
			show_usage(argv[0]);
			return ESMI_MULTI_ERROR;
		}

		if (is_string_number(argv[optind]) || is_string_number(argv[optind + 1])
		    || is_string_number(argv[optind + 2])) {
			printf("Option '-%c' requires 2nd, 3rd, 4th argument as valid"
					" numeric value\n\n", opt);
			show_usage(argv[0]);
			return ESMI_MULTI_ERROR;
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
		case 'c' :
			/* Set the boostlimit value for a given package */
			boostlimit = atoi(optarg);
			ret = epyc_setpkgperf(boostlimit);
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

		case 'A' :
			ret = show_smi_all_parameters();
			break;
		case 'h' :
			show_usage(argv[0]);
			return ESMI_MULTI_ERROR;
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
			return ESMI_MULTI_ERROR;
		default:
			printf(MAG "Try `%s --help' for more information."
						RESET "\n\n", argv[0]);
			return ESMI_MULTI_ERROR;
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

	return ret;
}
