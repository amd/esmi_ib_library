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
#include <cpuid.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>

#include <e_smi/e_smi.h>
#include <e_smi/e_smi_monitor.h>
#include <e_smi/e_smi_utils.h>

/*
 * total number of cores and sockets in the system
 * This information is going to be fixed for a boot cycle.
 */
struct system_metrics {
	uint32_t total_cores;		// total cores in a system.
	uint32_t total_sockets;		// total sockets in a system.
	uint32_t threads_per_core;	// threads per core in each cpu.
	uint32_t cpu_family;		// system cpu family.
	uint32_t cpu_model;		// system cpu model.
	int32_t  hsmp_proto_ver;	// hsmp protocol version.
	esmi_status_t init_status;	// esmi init status
	esmi_status_t energy_status;	// energy driver status
	esmi_status_t hsmp_status;	// hsmp driver status
	bool is_char_dev;		// is hsmp driver a character driver or sysfs based
	struct cpu_mapping *map;
};

static const struct system_metrics *psm;

struct cpu_mapping {
	int proc_id;
	int apic_id;
	int sock_id;
};

#define CPU_INFO_LINE_SIZE	1024
#define APICID_BIT		1
#define PHYSICALID_BIT		2
#define CPU_INFO_PATH		"/proc/cpuinfo"

static char *delim1 = ":";
static char *delim2 = "\n";
static const char *proc_str = "processor";
static const char *apic_str = "apicid";
static const char *node_str = "physical id";

extern char energymon_path[DRVPATHSIZ], hsmpmon_path[DRVPATHSIZ];

/*
 * To Calculate maximum possible number of cores and sockets,
 * cpu/present and node/possible entires may return 0-127.
 * Hence, parse till the last index value and read.
 */
static int read_index(char *filepath)
{
	FILE *fp;
	char buf[FILESIZ];
	int i, j;

	fp = fopen(filepath, "r");
	if (fp == NULL) {
		return -1;
	}

	if (fscanf(fp, "%s", buf) < 0) {
		buf[0] = '\0';
		fclose(fp);
		return -1;
	}

	for (i = 0, j = 0; buf[i] != '\0'; i++) {
                if (buf[i] < '0' || buf[i] > '9') {
                        j = i + 1;
                }
        }

	fclose(fp);
	return atoi(&buf[j]);
}

/*
 * To get the first online core on a given socket
 */
esmi_status_t esmi_first_online_core_on_socket(uint32_t sock_ind,
					       uint32_t *pcore_ind)
{
	char filepath[FILEPATHSIZ];
	FILE *fp;
	int i, socket;

	if (NULL == psm) {
		return ESMI_IO_ERROR;
	}
	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}
        if (NULL == pcore_ind) {
                return ESMI_ARG_PTR_NULL;
        }

	for (i = 0; i < psm->total_cores; i++) {
		snprintf(filepath, FILEPATHSIZ,
			 "%s/cpu%d/topology/physical_package_id",
			 CPU_SYS_PATH, i);

		fp = fopen(filepath, "r");
		if (fp == NULL) {
			continue;
		}

		if (fscanf(fp, "%d", &socket) < 0) {
			fclose(fp);
			continue;
		}

		if (sock_ind == socket) {
			//return first online core on given socket
			*pcore_ind = i;
			fclose(fp);
			return ESMI_SUCCESS;
		}
		fclose(fp);
	}
	return ESMI_IO_ERROR; //when no online core found on given socket
}

/*
 * Get appropriate error strins for the esmi error numbers
 */
char * esmi_get_err_msg(esmi_status_t esmi_err)
{
	switch(esmi_err) {
		case ESMI_SUCCESS:
			return "Success";
		case ESMI_NO_ENERGY_DRV:
			return "Energy driver not present";
		case ESMI_NO_HSMP_DRV:
			return "HSMP driver not present";
		case ESMI_NO_DRV:
			return "Both Energy, HSMP drivers not present";
		case ESMI_FILE_NOT_FOUND:
			return "Entry not found";
		case ESMI_DEV_BUSY:
			return "Device busy or core offline";
		case ESMI_PERMISSION:
			return "Invalid permissions";
		case ESMI_NOT_SUPPORTED:
			return "Not Supported";
		case ESMI_FILE_ERROR:
			return "File Error";
		case ESMI_INTERRUPTED:
			return "Task Interrupted";
		case ESMI_UNEXPECTED_SIZE:
		case ESMI_IO_ERROR:
			return "I/O Error";
		case ESMI_UNKNOWN_ERROR:
			return "Unknown error";
		case ESMI_ARG_PTR_NULL:
			return "Invalid buffer";
		case ESMI_NO_MEMORY:
			return "Memory Error";
		case ESMI_NOT_INITIALIZED:
			return "ESMI not initialized";
		case ESMI_INVALID_INPUT:
			return "Input value is invalid";
		default:
			return "Unknown error";
	}
}

/*
 * Map linux errors to esmi errors
 */
static esmi_status_t errno_to_esmi_status(int err)
{
	switch (err) {
		case 0:		return ESMI_SUCCESS;
		case EACCES:
		case EPERM:	return ESMI_PERMISSION;
		case ENOENT:	return ESMI_FILE_NOT_FOUND;
		case ENODEV:
		case EAGAIN:	return ESMI_DEV_BUSY;
		case EBADF:
		case EOF:
		case EISDIR:	return ESMI_FILE_ERROR;
		case EINTR:	return ESMI_INTERRUPTED;
		case EIO:	return ESMI_UNEXPECTED_SIZE;
		case ENOMEM:	return ESMI_NO_MEMORY;
		case EFAULT:	return ESMI_ARG_PTR_NULL;
		default:	return ESMI_UNKNOWN_ERROR;
	}
}

/*
 * Find the amd_energy driver is present and get the
 * path from driver initialzed sysfs path
 */
static esmi_status_t create_energy_monitor(void)
{
	static char hwmon_name[FILESIZ];

	if (find_energy(ENERGY_DEV_NAME, hwmon_name) != 0) {
		return ESMI_NO_ENERGY_DRV;
	}

	snprintf(energymon_path, sizeof(energymon_path), "%s/%s",
		 HWMON_PATH, hwmon_name);

	return ESMI_SUCCESS;
}

/*
 * Find the amd_hsmp driver is present and get the
 * path from driver initialzed sysfs path
 */
static esmi_status_t create_hsmp_monitor(struct system_metrics *sm)
{
	if (!access(HSMP_CHAR_DEVFILE_NAME, F_OK)) {
		sm->is_char_dev = 1;
		return ESMI_SUCCESS;
	}
	if (find_hsmp(CPU_SYS_PATH) == 0) {
		sm->is_char_dev = 0;
		snprintf(hsmpmon_path, sizeof(hsmpmon_path),
			 "%s/"HSMP_DEV_NAME, CPU_SYS_PATH);
		return ESMI_SUCCESS;
	}

	return ESMI_NO_HSMP_DRV;
}

static esmi_status_t create_cpu_mappings(struct system_metrics *sm)
{
	int flag = (APICID_BIT | PHYSICALID_BIT);
	size_t size = CPU_INFO_LINE_SIZE;
	int i = -1;
	char *tok;
	char *str;
	FILE *fp;

	str = malloc(CPU_INFO_LINE_SIZE);
	if (!str)
		return ESMI_NO_MEMORY;

	sm->map = malloc(sm->total_cores * sizeof(struct cpu_mapping));
	if (!sm->map)
		return ESMI_NO_MEMORY;

	fp = fopen(CPU_INFO_PATH, "r");
	if (!fp)
		return ESMI_FILE_ERROR;

	while (getline(&str, &size, fp) != -1) {
		if (tok = strtok(str, delim1)) {
			if (flag != (APICID_BIT | PHYSICALID_BIT)) {
				if(!strncmp(tok, node_str, strlen(node_str))) {
					tok  = strtok(NULL, delim2);
					sm->map[i].sock_id = atoi(tok);
					flag |= PHYSICALID_BIT;
					continue;
				}
				if(!strncmp(tok, apic_str, strlen(apic_str))) {
					tok  = strtok(NULL, delim2);
					sm->map[i].apic_id = atoi(tok);
					flag |= APICID_BIT;
				}

			} else {
				if(!strncmp(tok, proc_str, strlen(proc_str))) {
					i++;
					tok  = strtok(NULL, delim2);
					sm->map[i].proc_id = atoi(tok);
					flag = 0;
				}
			}
		}
	}

	free(str);

	fclose(fp);

	return ESMI_SUCCESS;
}

static esmi_status_t detect_packages(struct system_metrics *psysm)
{
	uint32_t eax, ebx, ecx,edx;
	int max_cores_socket, ret;

	if (NULL == psysm) {
		return ESMI_IO_ERROR;
	}
	if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
		return ESMI_IO_ERROR;
	}
	psysm->cpu_family = ((eax >> 8) & 0xf) + ((eax >> 20) & 0xff);
	psysm->cpu_model = ((eax >> 16) & 0xf) * 0x10 + ((eax >> 4) & 0xf);

	if (!__get_cpuid(0x08000001e, &eax, &ebx, &ecx, &edx)) {
		return ESMI_IO_ERROR;
	}
	psysm->threads_per_core = ((ebx >> 8) & 0xff) + 1;

	ret = read_index(CPU_COUNT_PATH);
	if(ret < 0) {
		return ESMI_IO_ERROR;
	}
	psysm->total_cores = ret + 1;

	if (!__get_cpuid(0x1, &eax, &ebx, &ecx, &edx))
		return ESMI_IO_ERROR;

	max_cores_socket = ((ebx >> 16) & 0xff);

	/* Number of sockets in the system */
	psysm->total_sockets = psysm->total_cores / max_cores_socket;

	return ESMI_SUCCESS;
}

/*
 * First initialization function to be executed and confirming
 * all the monitor or driver objects should be initialized or not
 */
esmi_status_t esmi_init()
{
	esmi_status_t ret;
	static struct system_metrics sm;

	sm.init_status = ESMI_NOT_INITIALIZED;
	sm.energy_status = ESMI_NOT_INITIALIZED;
	sm.hsmp_status = ESMI_NOT_INITIALIZED;

	ret = detect_packages(&sm);
	if (ret != ESMI_SUCCESS) {
		return ret;
	}

	ret = create_energy_monitor();
	if (ret == ESMI_SUCCESS) {
		sm.energy_status = ESMI_INITIALIZED;
	}

	ret = create_hsmp_monitor(&sm);
	if (ret == ESMI_SUCCESS) {
		if (sm.is_char_dev) {
			ret = create_cpu_mappings(&sm);
			if (ret != ESMI_SUCCESS)
				return ret;

			struct hsmp_message msg = { 0 };
			msg.msg_id = HSMP_PROTO_VER_TYPE;
			msg.response_sz = 1;
			msg.sock_ind = 0;
			if (!hsmp_xfer(&msg, O_RDONLY)) {
				sm.hsmp_status = ESMI_INITIALIZED;
				sm.hsmp_proto_ver = msg.response[0];
			}
		} else {
			if (!hsmp_read32(HSMP_PROTO_VER_TYPE, 0, &sm.hsmp_proto_ver))
				sm.hsmp_status = ESMI_INITIALIZED;
		}
	}

	if (sm.energy_status & sm.hsmp_status) {
		sm.init_status = ESMI_NO_DRV;
	} else {
		sm.init_status = ESMI_INITIALIZED;
	}
	psm = &sm;

	return sm.init_status;
}

void esmi_exit(void)
{
	if (psm->map)
		free(psm->map);
	psm = NULL;

	return;
}

#define CHECK_ESMI_GET_INPUT(parg) \
	if (NULL == psm) {\
		return ESMI_IO_ERROR;\
	}\
	if (psm->init_status == ESMI_NOT_INITIALIZED) {\
		return ESMI_NOT_INITIALIZED;\
	}\
	if (NULL == parg) {\
		return ESMI_ARG_PTR_NULL;\
	}\

/* get cpu family */
esmi_status_t esmi_cpu_family_get(uint32_t *family)
{
	CHECK_ESMI_GET_INPUT(family);

	*family = psm->cpu_family;

	return ESMI_SUCCESS;
}

/* get cpu model */
esmi_status_t esmi_cpu_model_get(uint32_t *model)
{
	CHECK_ESMI_GET_INPUT(model);

	*model = psm->cpu_model;

	return ESMI_SUCCESS;
}

/* get number of threads per core */
esmi_status_t esmi_threads_per_core_get(uint32_t *threads)
{
	CHECK_ESMI_GET_INPUT(threads);

	*threads = psm->threads_per_core;

	return ESMI_SUCCESS;
}

/* get number of cpus available */
esmi_status_t esmi_number_of_cpus_get(uint32_t *cpus)
{
	CHECK_ESMI_GET_INPUT(cpus);

	*cpus = psm->total_cores;

	return ESMI_SUCCESS;
}

/* get number of sockets available */
esmi_status_t esmi_number_of_sockets_get(uint32_t *sockets)
{
	CHECK_ESMI_GET_INPUT(sockets);
	*sockets = psm->total_sockets;

	return ESMI_SUCCESS;
}

#define CHECK_ENERGY_GET_INPUT(parg) \
	if (NULL == psm) {\
		return ESMI_IO_ERROR;\
	}\
	if (psm->init_status == ESMI_NOT_INITIALIZED) {\
		return ESMI_NOT_INITIALIZED;\
	} else if (psm->energy_status == ESMI_NOT_INITIALIZED) {\
		return ESMI_NO_ENERGY_DRV;\
	}\
	if (NULL == parg) {\
		return ESMI_ARG_PTR_NULL;\
	}\

#define CHECK_HSMP_GET_INPUT(parg) \
	if (NULL == psm) {\
		return ESMI_IO_ERROR;\
	}\
	if (psm->init_status == ESMI_NOT_INITIALIZED) {\
		return ESMI_NOT_INITIALIZED;\
	} else if (psm->hsmp_status == ESMI_NOT_INITIALIZED) {\
		return ESMI_NO_HSMP_DRV;\
	}\
	if (NULL == parg) {\
		return ESMI_ARG_PTR_NULL;\
	}\

#define CHECK_HSMP_INPUT() \
	if (NULL == psm) {\
		return ESMI_IO_ERROR;\
	}\
	if (psm->init_status == ESMI_NOT_INITIALIZED) {\
		return ESMI_NOT_INITIALIZED;\
	} else if (psm->hsmp_status == ESMI_NOT_INITIALIZED) {\
		return ESMI_NO_HSMP_DRV;\
	}\

/*
 * Energy Monitor functions
 *
 * Function to get the enenrgy of the core with provided core index
 */
esmi_status_t esmi_core_energy_get(uint32_t core_ind, uint64_t *penergy)
{
	int ret;

	CHECK_ENERGY_GET_INPUT(penergy);
	if (core_ind >= psm->total_cores) {
		return ESMI_INVALID_INPUT;
	}
	core_ind %= psm->total_cores/psm->threads_per_core;

	/*
	 * The hwmon enumeration of energy%d_input entries starts
	 * from 1.
	 */
	ret = read_energy(ENERGY_TYPE, core_ind + 1, penergy);

	return errno_to_esmi_status(ret);
}

/*
 * Function to get the enenrgy of the socket with provided socket index
 */
esmi_status_t esmi_socket_energy_get(uint32_t sock_ind, uint64_t *penergy)
{
	int ret;

	CHECK_ENERGY_GET_INPUT(penergy);
	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	/*
	 * The hwmon enumeration of socket energy entries starts
	 * from "total_cores/threads_per_core + sock_ind + 1".
	 */
	ret = read_energy(ENERGY_TYPE,
		(psm->total_cores/psm->threads_per_core) + sock_ind + 1,
			  penergy);

	return errno_to_esmi_status(ret);
}

/*
 * Function to get the enenrgy of cpus and sockets.
 */
esmi_status_t esmi_all_energies_get(uint64_t *penergy)
{
	int ret;
	uint32_t cpus;

	CHECK_ENERGY_GET_INPUT(penergy);
	cpus = psm->total_cores / psm->threads_per_core;

	ret = batch_read_energy(ENERGY_TYPE, penergy, cpus);

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_smu_fw_version_get(struct smu_fw_version *smu_fw)
{
	int ret;

	CHECK_HSMP_GET_INPUT(smu_fw);

	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		msg.msg_id = SMU_FW_VERSION_TYPE;
		msg.response_sz = 1;
		msg.sock_ind = 0;
		ret = hsmp_xfer(&msg, O_RDONLY);
		if (!ret)
			*(uint32_t *)smu_fw = msg.response[0];
	} else {
		ret = hsmp_read32(SMU_FW_VERSION_TYPE, 0, (uint32_t *)smu_fw);
	}

	return errno_to_esmi_status(ret);
}

/*
 * Power Monitoring Functions
 *
 * Function to get the Average Power consumed of the Socket with provided
 * socket index
 */
esmi_status_t esmi_socket_power_get(uint32_t sock_ind, uint32_t *ppower)
{
	int ret;

	CHECK_HSMP_GET_INPUT(ppower);

	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		msg.msg_id = SOCKET_POWER_TYPE;
		msg.response_sz = 1;
		msg.sock_ind = sock_ind;
		ret = hsmp_xfer(&msg, O_RDONLY);
		if (!ret)
			*ppower = msg.response[0];
	} else {
		ret = hsmp_read32(SOCKET_POWER_TYPE, sock_ind, ppower);
	}

	return errno_to_esmi_status(ret);
}

/*
 * Function to get the Power Limit of the Socket with provided
 * socket index
 */
esmi_status_t esmi_socket_power_cap_get(uint32_t sock_ind, uint32_t *pcap)
{
	int ret;

	CHECK_HSMP_GET_INPUT(pcap);

	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		msg.msg_id = R_SOCKET_POWER_LIMIT_TYPE;
		msg.response_sz = 1;
		msg.sock_ind = sock_ind;
		ret = hsmp_xfer(&msg, O_RDONLY);
		if (!ret)
			*pcap = msg.response[0];
	} else {
		ret = hsmp_read32(R_SOCKET_POWER_LIMIT_TYPE, sock_ind, pcap);
	}

	return errno_to_esmi_status(ret);
}

/*
 * Function to get the Maximum Power Limit of the Socket with provided
 * socket index
 */
esmi_status_t esmi_socket_power_cap_max_get(uint32_t sock_ind, uint32_t *pmax)
{
	int ret;

	CHECK_HSMP_GET_INPUT(pmax);

	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		msg.msg_id = SOCKET_POWER_LIMIT_MAX_TYPE;
		msg.response_sz = 1;
		msg.sock_ind = sock_ind;
		ret = hsmp_xfer(&msg, O_RDONLY);
		if (!ret)
			*pmax = msg.response[0];
	} else {
		ret = hsmp_read32(SOCKET_POWER_LIMIT_MAX_TYPE, sock_ind, pmax);
	}

	return errno_to_esmi_status(ret);
}

/*
 * Power Control Function
 *
 * Function to Set or Control the Power limit of the
 * Socket with provided socket index and limit to be set
 */
esmi_status_t esmi_socket_power_cap_set(uint32_t sock_ind, uint32_t cap)
{
	int ret;

	CHECK_HSMP_INPUT();

	/* TODO check against minimum limit */
	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		msg.msg_id = W_SOCKET_POWER_LIMIT_TYPE;
		msg.num_args = 1;
		msg.sock_ind = sock_ind;
		msg.args[0] = cap;
		ret = hsmp_xfer(&msg, O_WRONLY);
	} else {

		ret = hsmp_write32(W_SOCKET_POWER_LIMIT_TYPE, sock_ind, cap);
	}

	return errno_to_esmi_status(ret);
}

/*
 * Performance (Boost limit) Monitoring Function
 *
 * Function to get the boostlimit of the Core with provided
 * Core index
 */
esmi_status_t esmi_core_boostlimit_get(uint32_t core_ind,
				       uint32_t *pboostlimit)
{
	int ret;

	CHECK_HSMP_GET_INPUT(pboostlimit);

	if (core_ind >= psm->total_cores) {
		return ESMI_INVALID_INPUT;
	}

	if (!psm->map)
		return ESMI_IO_ERROR;

	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		msg.msg_id = R_CORE_BOOSTLIMIT_TYPE;
		msg.num_args = 1;
		msg.response_sz = 1;
		msg.sock_ind = psm->map[core_ind].sock_id;
		msg.args[0] = psm->map[core_ind].apic_id;
		ret = hsmp_xfer(&msg, O_RDONLY);
		if (!ret)
			*pboostlimit = msg.response[0];
	} else {
		ret = hsmp_read32(R_CORE_BOOSTLIMIT_TYPE, core_ind, pboostlimit);
	}

	return errno_to_esmi_status(ret);
}

/*
 * Performance (Boost limit) Control Function
 *
 * Function to Set or Control the freq limits of the core
 * with provided core index and boostlimit value to be set
 */
esmi_status_t esmi_core_boostlimit_set(uint32_t core_ind,
				       uint32_t boostlimit)
{
	int ret;

	CHECK_HSMP_INPUT();

	/* TODO check boostlimit against a valid range */
	if (core_ind >= psm->total_cores) {
		return ESMI_INVALID_INPUT;
	}

	if (!psm->map)
		return ESMI_IO_ERROR;

	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		msg.msg_id = W_CORE_BOOSTLIMIT_TYPE;
		msg.num_args = 1;
		msg.sock_ind = psm->map[core_ind].sock_id;
		msg.args[0] = (psm->map[core_ind].apic_id << 16) | boostlimit;
		ret = hsmp_xfer(&msg, O_WRONLY);
	} else {
		ret = hsmp_write32(W_CORE_BOOSTLIMIT_TYPE, core_ind, boostlimit);
	}

	return errno_to_esmi_status(ret);
}

/*
 * Function to Set or Control the freq limits of the socket
 * with provided socket index and boostlimit value to be set
 */
esmi_status_t esmi_socket_boostlimit_set(uint32_t sock_ind,
					 uint32_t boostlimit)
{
	int ret;

	CHECK_HSMP_INPUT();

	/* TODO check boostlimit against a valid range */
	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		msg.msg_id = SOCKET_BOOSTLIMIT_TYPE;
		msg.num_args = 1;
		msg.sock_ind = sock_ind;
		msg.args[0] = boostlimit;
		ret = hsmp_xfer(&msg, O_WRONLY);
	} else {
		ret = hsmp_write32(SOCKET_BOOSTLIMIT_TYPE, sock_ind, boostlimit);
	}

	return errno_to_esmi_status(ret);
}

/*
 * Function to Set or Control the freq limits of the
 * entire package with provided boostlimit value to be set
 */
esmi_status_t esmi_package_boostlimit_set(uint32_t boostlimit)
{
	int ret, i;

	CHECK_HSMP_INPUT();

	/* TODO check boostlimit against a valid range */

	if (!psm->is_char_dev) {
		ret = hsmp_write32(PKG_BOOSTLIMIT_TYPE, 0, boostlimit);
		return errno_to_esmi_status(ret);
	}

	for (i = 0; i < psm->total_sockets; i++) {
		struct hsmp_message msg = { 0 };
		msg.msg_id = SOCKET_BOOSTLIMIT_TYPE;
		msg.num_args = 1;
		msg.sock_ind = i;
		msg.args[0] = boostlimit;
		ret = hsmp_xfer(&msg, O_WRONLY);
		if (ret)
			return errno_to_esmi_status(ret);
	}

	return ESMI_SUCCESS;
}

esmi_status_t esmi_prochot_status_get(uint32_t sock_ind, uint32_t *prochot)
{
	char hot[9];
	int ret;

	CHECK_HSMP_GET_INPUT(prochot);

	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		msg.msg_id = PROCHOT_STATUS_TYPE;
		msg.response_sz = 1;
		msg.sock_ind = sock_ind;
		ret = hsmp_xfer(&msg, O_RDONLY);
		if (!ret)
			*prochot = msg.response[0];
	} else {
		ret = hsmp_readstr(PROCHOT_STATUS_TYPE, sock_ind, hot, 9);
		if (!ret)
			*prochot = strncmp(hot, "inactive", 8) ? 1 : 0;
	}

	return errno_to_esmi_status(ret);
}

/* xgmi link is used in multi socket system
 * width can be set to 2/8/16 lanes
 */
esmi_status_t esmi_xgmi_width_set(uint8_t min, uint8_t max)
{
	uint16_t width;
	int drv_val;
	int ret;
	int i;

	CHECK_HSMP_INPUT();

	if (psm->total_sockets < 2)
		return ESMI_NOT_SUPPORTED;

	if ((min > max) || (min > 2) || (max > 2))
		return ESMI_INVALID_INPUT;

	width = (min << 8) | max;
	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		for (i = 0; i < psm->total_sockets; i++) {
			msg.msg_id = XGMI_WIDTH_TYPE;
			msg.num_args = 1;
			msg.args[0] = width;
			msg.sock_ind = i;
			ret = hsmp_xfer(&msg, O_WRONLY);
			if (ret)
				return errno_to_esmi_status(ret);
		}
	} else {
		/* HSMP sysfs driver accepts -1, 0, 1, 2 value as input and based on this,
		 * driver sets min and max width as (0,2), (2, 2), (1,1),(0,0) respectively
		 * width = min << 8 | max will arrive as following cases */
		switch (width) {
		case 0x2:
			drv_val = -1;
			break;
		case 0x202:
			drv_val = 0;
			break;
		case 0x101:
			drv_val = 1;
			break;
		case 0x0:
			drv_val = 2;
			break;
		default:
			return ESMI_INVALID_INPUT;
		}
		ret = hsmp_write32(XGMI_WIDTH_TYPE, 0, drv_val);
	}

	return errno_to_esmi_status(ret);
}

/* enable APB, no arguments */
esmi_status_t esmi_apb_enable(uint32_t sock_ind)
{
	int ret;

	CHECK_HSMP_INPUT();

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	/*
	 * While the socket is in PC6 or if PROCHOT_L is
	 * asserted, the lowest DF P-state (highest value) is enforced
	 * regardless of the APBEnable/APBDisable
	 */
	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		msg.msg_id = EN_DF_PSTATE_TYPE;
		msg.sock_ind = sock_ind;
		ret = hsmp_xfer(&msg, O_WRONLY);
	} else {
		/* For sysfs, -1 indicates driver to enable apb */
		ret = hsmp_write_s32(EN_DF_PSTATE_TYPE, sock_ind, -1);
	}

	return errno_to_esmi_status(ret);
}

/* disable APB, user can set 0 ~ 3 */
esmi_status_t esmi_apb_disable(uint32_t sock_ind, uint8_t pstate)
{
	int ret;

	CHECK_HSMP_INPUT();

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	if (pstate > 3)
		return ESMI_INVALID_INPUT;

	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		msg.msg_id = DIS_DF_PSTATE_TYPE;
		msg.num_args = 1;
		msg.sock_ind = sock_ind;
		msg.args[0] = pstate;
		ret = hsmp_xfer(&msg, O_WRONLY);
	} else {
		ret = hsmp_write32(DIS_DF_PSTATE_TYPE, sock_ind, pstate);
	}

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_fclk_mclk_get(uint32_t sock_ind,
				 uint32_t *fclk, uint32_t *mclk)
{
	int ret;
	uint64_t clk;

	CHECK_HSMP_INPUT();

        if (!(fclk && mclk))
                return ESMI_ARG_PTR_NULL;
	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		msg.msg_id = FCLK_MEMCLK_TYPE;
		msg.response_sz = 2;
		msg.sock_ind = sock_ind;
		ret = hsmp_xfer(&msg, O_RDONLY);
		if (!ret) {
			*fclk = msg.response[0];
			*mclk = msg.response[1];
		}
	} else {
		ret = hsmp_read64(FCLK_MEMCLK_TYPE, sock_ind, &clk);
		if (!ret) {
			*fclk = clk & 0xFFFFFFFF;  // as per hsmp_driver fclk is [31:0]
			*mclk = clk >> 32; // as per hsmp_driver mclk is [63:32]
		}
	}

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_cclk_limit_get(uint32_t sock_ind, uint32_t *cclk)
{
	int ret;

	CHECK_HSMP_GET_INPUT(cclk);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		msg.msg_id = CCLK_LIMIT_TYPE;
		msg.response_sz = 1;
		msg.sock_ind = sock_ind;
		ret = hsmp_xfer(&msg, O_RDONLY);
		if (!ret)
			*cclk = msg.response[0];
	} else {
		ret = hsmp_read32(CCLK_LIMIT_TYPE, sock_ind, cclk);
	}

	return errno_to_esmi_status(ret);
}

/*
 * c0_Residency Monitoring Function
 *
 * Function to get the c0_residency of the socket with provided
 * socket index
 */
esmi_status_t esmi_socket_c0_residency_get(uint32_t sock_ind,
					   uint32_t *pc0_residency)
{
	int ret;

	CHECK_HSMP_GET_INPUT(pc0_residency);
	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		msg.msg_id = SOCKET_C0_RESIDENCY_TYPE;
		msg.response_sz = 1;
		msg.sock_ind = sock_ind;
		ret = hsmp_xfer(&msg, O_RDONLY);
		if (!ret)
			*pc0_residency = msg.args[0];
	} else {
		ret = hsmp_read32(SOCKET_C0_RESIDENCY_TYPE, sock_ind, pc0_residency);
	}

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_socket_lclk_dpm_level_set(uint32_t sock_ind, uint8_t nbio_id,
					     uint8_t min, uint8_t max)
{
	struct hsmp_message msg = { 0 };
	uint32_t dpm_val;
	int ret;

	CHECK_HSMP_INPUT();
	if (!psm->is_char_dev)
		return ESMI_NOT_SUPPORTED;

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;
	if (nbio_id > 3)
		return ESMI_INVALID_INPUT;
	if ((min > max) || (min > 3) || (max > 3))
		return ESMI_INVALID_INPUT;

	dpm_val = (nbio_id << 16) | (max << 8) | min;

	msg.msg_id = LCLKDPM_LEVEL;
	msg.num_args = 1;
	msg.sock_ind = sock_ind;
	msg.args[0] = dpm_val;
	ret = hsmp_xfer(&msg, O_WRONLY);

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_ddr_bw_get(struct ddr_bw_metrics *ddr_bw)
{
	uint32_t bw;
	int ret;

	CHECK_HSMP_GET_INPUT(ddr_bw);

	if (psm->hsmp_proto_ver < 3) {
		return ESMI_NO_HSMP_SUP;
	}

	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		msg.msg_id = DDR_BW_TYPE;
		msg.response_sz = 1;
		msg.sock_ind = 0;
		ret = hsmp_xfer(&msg, O_RDONLY);
		if (ret)
			return errno_to_esmi_status(ret);
		bw = msg.response[0];
	} else {
		ret = hsmp_read32(DDR_BW_TYPE, 0, &bw);
		if (ret != ESMI_SUCCESS)
			return errno_to_esmi_status(ret);
	}

	ddr_bw->max_bw = bw >> 20;
	ddr_bw->utilized_bw = (bw >> 8) & 0xFFF;
	ddr_bw->utilized_pct = bw & 0xFF;

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_socket_temperature_get(uint32_t sock_ind, uint32_t *ptmon)
{
	esmi_status_t ret;

	if (psm->hsmp_proto_ver != 4)
		return ESMI_NO_HSMP_SUP;

	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	CHECK_HSMP_GET_INPUT(ptmon);

	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		uint32_t int_part, fract_part;

		msg.msg_id = SOCKET_TEMP_MONITOR_TYPE;
		msg.response_sz = 1;
		msg.sock_ind = sock_ind;
		ret = hsmp_xfer(&msg, O_RDONLY);
		if (!ret) {
			/* convert temperature to milli degree celsius */
			int_part = ((msg.response[0] >> 8) & 0xFF) * 1000;
			fract_part = ((msg.response[0] >> 5) & 0x7) * 125;
			*ptmon = int_part + fract_part;
		}
	} else {
		ret = hsmp_read32(SOCKET_TEMP_MONITOR_TYPE, sock_ind, ptmon);
	}

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_hsmp_proto_ver_get(uint32_t *proto_ver)
{
	int ret;

	CHECK_HSMP_GET_INPUT(proto_ver);

	if (psm->is_char_dev) {
		struct hsmp_message msg = { 0 };
		msg.msg_id = HSMP_PROTO_VER_TYPE;
		msg.response_sz = 1;
		msg.sock_ind = 0;
		ret = hsmp_xfer(&msg, O_RDONLY);
		if (!ret)
			*proto_ver = msg.response[0];
	} else {
		ret = hsmp_read32(HSMP_PROTO_VER_TYPE, 0, proto_ver);
	}

	return errno_to_esmi_status(ret);
}
