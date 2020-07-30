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

#include <e_smi/e_smi.h>
#include <e_smi/e_smi_monitor.h>
#include <e_smi/e_smi_utils.h>

/*
 * total number of cores and sockets in the system
 * This number is not going to change for a power cycle.
 */
static uint32_t total_cores, total_sockets, threads_per_core;
static uint32_t cpu_family, cpu_model;

/*
 * Mark them uninitalize to start with
 */
static esmi_status_t init_status   = ESMI_NOT_INITIALIZED;
static esmi_status_t energy_status = ESMI_NOT_INITIALIZED;
static esmi_status_t hsmp_status   = ESMI_NOT_INITIALIZED;

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
	}

	for (i = 0, j = 0; buf[i] != '\0'; i++) {
                if (buf[i] < '0' || buf[i] > '9') {
                        j = i + 1;
                }
        }

	return atoi(&buf[j]);
}

/*
 * To get the first online core on a given socket
 */
int esmi_get_online_core_on_socket(int socket_id)
{
	char filepath[FILEPATHSIZ];
	FILE *fp;
	int i, socket;

	for (i = 0; i < total_cores; i++) {
		snprintf(filepath, FILEPATHSIZ,
			 "%s/cpu%d/topology/physical_package_id",
			 CPU_PATH, i);

		fp = fopen(filepath, "r");
		if (fp == NULL) {
			continue;
		}

		if (fscanf(fp, "%d", &socket) < 0) {
			continue;
		}

		if (socket_id == socket) {
			return i; //return first online core on given socket
		}
	}
	return -1; //when no online core found on given socket
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
static esmi_status_t create_hsmp_monitor(void)
{
	if (find_hsmp(CPU_PATH) != 0) {
		return ESMI_NO_HSMP_DRV;
	}

	snprintf(hsmpmon_path, sizeof(hsmpmon_path),
		 "%s/"HSMP_DEV_NAME, CPU_PATH);

	return ESMI_SUCCESS;
}

static esmi_status_t detect_packages()
{
	uint32_t eax, ebx, ecx,edx;
	int ret;

	if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
		return ESMI_IO_ERROR;
	}
	cpu_family = ((eax >> 8) & 0xf) + ((eax >> 20) & 0xff);
	cpu_model = ((eax >> 16) & 0xf) * 0x10 + ((eax >> 4) & 0xf);

	if (!__get_cpuid(0x08000001e, &eax, &ebx, &ecx, &edx)) {
		return ESMI_IO_ERROR;
	}
	threads_per_core = ((ebx >> 8) & 0xff) + 1;

	ret = read_index(CPU_COUNT_PATH);
	if(ret < 0) {
		return ESMI_IO_ERROR;
	}
	total_cores = ret + 1;

	ret = read_index(SOCKET_COUNT_PATH);
	if (ret < 0) {
		return ESMI_IO_ERROR;
	}
	total_sockets = ret + 1;

	return ESMI_SUCCESS;
}

/*
 * First initialization function to be executed and confirming
 * all the monitor or driver objects should be initialized or not
 */
esmi_status_t esmi_init()
{
	esmi_status_t ret;

	ret = detect_packages();
	if (ret != ESMI_SUCCESS) {
		return ret;
	}

	ret = create_energy_monitor();
	if (ret == ESMI_SUCCESS) {
		energy_status = ESMI_INITIALIZED;
	}

	ret = create_hsmp_monitor();
	if (ret == ESMI_SUCCESS) {
		hsmp_status = ESMI_INITIALIZED;
	}

	if (energy_status & hsmp_status) {
		init_status = ESMI_NO_DRV;
	} else {
		init_status = ESMI_INITIALIZED;
	}

	return init_status;
}

void esmi_exit(void)
{
	init_status = ESMI_NOT_INITIALIZED;
}

/* get cpu family */
esmi_status_t esmi_cpu_family_get(uint32_t *family)
{
	if (init_status == ESMI_NOT_INITIALIZED) {
		return ESMI_NOT_INITIALIZED;
	}
	*family = cpu_family;

	return ESMI_SUCCESS;
}

/* get cpu model */
esmi_status_t esmi_cpu_model_get(uint32_t *model)
{
	if (init_status == ESMI_NOT_INITIALIZED) {
		return ESMI_NOT_INITIALIZED;
	}
	*model = cpu_model;

	return ESMI_SUCCESS;
}

/* get number of threads per core */
esmi_status_t esmi_threads_per_core_get(uint32_t *threads)
{
	if (init_status == ESMI_NOT_INITIALIZED) {
		return ESMI_NOT_INITIALIZED;
	}
	*threads = threads_per_core;

	return ESMI_SUCCESS;
}

/* get number of cpus available */
esmi_status_t esmi_number_of_cpus_get(uint32_t *cpus)
{
	if (init_status == ESMI_NOT_INITIALIZED) {
		return ESMI_NOT_INITIALIZED;
	}
	*cpus = total_cores;

	return ESMI_SUCCESS;
}

/* get number of sockets available */
esmi_status_t esmi_number_of_sockets_get(uint32_t *sockets)
{
	if (init_status == ESMI_NOT_INITIALIZED) {
		return ESMI_NOT_INITIALIZED;
	}
	*sockets = total_sockets;

	return ESMI_SUCCESS;
}

static esmi_status_t energy_get(uint32_t sensor_id, uint64_t *penergy)
{
	int ret;

	if (init_status == ESMI_NOT_INITIALIZED) {
		return ESMI_NOT_INITIALIZED;
	} else if (energy_status == ESMI_NOT_INITIALIZED) {
		return ESMI_NO_ENERGY_DRV;
	}

	if (NULL == penergy) {
		return ESMI_ARG_PTR_NULL;
	}

	/*
	 * The hwmon enumeration of energy%d_input entries starts
	 * from 1.
	 */
	ret = read_energy(ENERGY_TYPE, sensor_id + 1, penergy);

	return errno_to_esmi_status(ret);
}

static esmi_status_t hsmp_read(monitor_types_t type,
			       uint32_t sensor_id, uint32_t *pval)
{
	int ret;

	if (init_status == ESMI_NOT_INITIALIZED) {
                return ESMI_NOT_INITIALIZED;
        } else if (hsmp_status == ESMI_NOT_INITIALIZED) {
                return ESMI_NO_HSMP_DRV;
        }

        if (NULL == pval) {
                return ESMI_ARG_PTR_NULL;
        }

	ret = hsmp_read32(type, sensor_id, pval);
	return errno_to_esmi_status(ret);
}

static esmi_status_t hsmp_write(monitor_types_t type,
				uint32_t sensor_id, uint32_t val)
{
	int ret;

        if (init_status == ESMI_NOT_INITIALIZED) {
                return ESMI_NOT_INITIALIZED;
        } else if (hsmp_status == ESMI_NOT_INITIALIZED) {
                return ESMI_NO_HSMP_DRV;
        }

	ret = hsmp_write32(type, sensor_id, val);
	return errno_to_esmi_status(ret);
}

/*
 * Energy Monitor functions
 *
 * Function to get the enenrgy of the core with provided core index
 */
esmi_status_t esmi_core_energy_get(uint32_t core_ind, uint64_t *penergy)
{
	if (core_ind >= total_cores) {
		return ESMI_INVALID_INPUT;
	}
	core_ind %= total_cores/threads_per_core;

	return energy_get(core_ind, penergy);
}

/*
 * Function to get the enenrgy of the socket with provided socket index
 */
esmi_status_t esmi_socket_energy_get(uint32_t sock_ind, uint64_t *penergy)
{
	if (sock_ind >= total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	/*
	 * The hwmon enumeration of socket energy entries starts
	 * from "total_cores/threads_per_core + sock_ind".
	 */
	return energy_get((total_cores/threads_per_core) + sock_ind, penergy);
}

/*
 * Power Monitoring Functions
 *
 * Function to get the Average Power consumed of the Socket with provided
 * socket index
 */
esmi_status_t esmi_socket_power_avg_get(uint32_t sock_ind, uint32_t *ppower)
{
	if (sock_ind >= total_sockets) {
		return ESMI_INVALID_INPUT;
	}

        return hsmp_read(SOCKET_POWER_TYPE, sock_ind, ppower);
}

/*
 * Function to get the Power Limit of the Socket with provided
 * socket index
 */
esmi_status_t esmi_socket_power_cap_get(uint32_t sock_ind, uint32_t *pcap)
{
	if (sock_ind >= total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	return hsmp_read(SOCKET_POWER_LIMIT_TYPE, sock_ind, pcap);
}

/*
 * Function to get the Maximum Power Limit of the Socket with provided
 * socket index
 */
esmi_status_t esmi_socket_power_cap_max_get(uint32_t sock_ind, uint32_t *pmax)
{
	if (sock_ind >= total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	return hsmp_read(SOCKET_POWER_LIMIT_MAX_TYPE, sock_ind, pmax);
}

/*
 * Power Control Function
 *
 * Function to Set or Control the Power limit of the
 * Socket with provided socket index and limit to be set
 */
esmi_status_t esmi_socket_power_cap_set(uint32_t sock_ind, uint32_t cap)
{
	/* TODO check against minimum limit */
	if (sock_ind >= total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	return hsmp_write(SOCKET_POWER_LIMIT_TYPE, sock_ind, cap);
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
	if (core_ind >= total_cores) {
		return ESMI_INVALID_INPUT;
	}

	return hsmp_read(CORE_BOOSTLIMIT_TYPE, core_ind, pboostlimit);
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
	/* TODO check boostlimit against a valid range */
	if (core_ind >= total_cores) {
		return ESMI_INVALID_INPUT;
	}

	return hsmp_write(CORE_BOOSTLIMIT_TYPE, core_ind, boostlimit);
}

/*
 * Function to Set or Control the freq limits of the socket
 * with provided socket index and boostlimit value to be set
 */
esmi_status_t esmi_socket_boostlimit_set(uint32_t sock_ind,
					 uint32_t boostlimit)
{
	/* TODO check boostlimit against a valid range */
	if (sock_ind >= total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	return hsmp_write(SOCKET_BOOSTLIMIT_TYPE, sock_ind, boostlimit);
}

/*
 * Function to Set or Control the freq limits of the
 * entire package with provided boostlimit value to be set
 */
esmi_status_t esmi_package_boostlimit_set(uint32_t boostlimit)
{
	/* TODO check boostlimit against a valid range */
	return hsmp_write(PKG_BOOSTLIMIT_TYPE, 0, boostlimit);
}

/*
 * Tctl Monitoring Function
 *
 * Function to get the tctl of the socket with provided
 * socket index
 */
esmi_status_t esmi_socket_tctl_get(uint32_t sock_ind, uint32_t *ptctl)
{
	if (sock_ind >= total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	return ESMI_NOT_SUPPORTED;
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
	if (sock_ind >= total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	return ESMI_NOT_SUPPORTED;
}
