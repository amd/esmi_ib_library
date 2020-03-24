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
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <e_smi/e_smi.h>
#include <e_smi/e_smi_monitor.h>
#include <e_smi/e_smi_utils.h>

/* NODE FILENAMES */
static char energy_file[] = "energy#_input";
static char pkg_boostlimit_file[] = "boost_limit";
static char core_boostlimit_file[] = "cpu#/boost_limit";
static char socket_boostlimit_file[] = "socket#/boost_limit";
static char socket_power_file[] = "socket#/power";
static char socket_power_limit_file[] = "socket#/power_limit";
static char socket_power_limit_max_file[] = "socket#/power_limit_max";
static char socket_temp_ctrl_file[] = "socket#/tctl";
static char socket_c0_residency_file[] = "socket#/c0_residency";

static char *filenames[MONITOR_TYPE_MAX] = {energy_file,
					    pkg_boostlimit_file,
					    core_boostlimit_file,
					    socket_boostlimit_file,
					    socket_power_file,
					    socket_power_limit_file,
					    socket_power_limit_max_file,
					    socket_temp_ctrl_file,
					    socket_c0_residency_file,
};

int find_energy(char *devname, char *hwmon_name)
{
	DIR *pdir;
	struct dirent *pdentry;
	char name[FILESIZ];
	FILE *fptr;
	char filepath[FILEPATHSIZ];

	if (NULL == hwmon_name) {
		return EFAULT;
	}

	pdir = opendir(HWMON_PATH);
	if (NULL == pdir) {
		return errno;
	}

	while ((pdentry = readdir(pdir))) {
		snprintf(filepath, FILEPATHSIZ, "%s/%s/name",
			 HWMON_PATH, pdentry->d_name);
		fptr = fopen(filepath, "r");
		if (NULL == fptr) {
			continue;
		} else {
			if (fscanf(fptr, "%s", name) < 0) {
				name[0] = '\0';
			}
			fclose(fptr);
			if (strcmp(name, devname) == 0) {
				strcpy(hwmon_name, pdentry->d_name);
				closedir(pdir);
				return ESMI_SUCCESS;
			}
		}
	}
	closedir(pdir);
	return ENOENT;
}

int find_hsmp(const char *path)
{
	DIR *pdir;
	struct dirent *pdentry;

	pdir = opendir(path);
	if (NULL == pdir) {
		return errno;
	}
	while ((pdentry = readdir(pdir))) {
		if (strcmp(pdentry->d_name, HSMP_DEV_NAME) == 0) {
			return ESMI_SUCCESS;
		}
	}
	return ENOENT;
}

/*
 * This function does not validate the arguments, does not return
 * errors. Always, used with known values.
 */
static void str_replace(char *buf, int buf_size, char ch, uint32_t num)
{
	int i;
	char *org;

	for (i = 0; *buf != '\0'; i++, buf++) {
		if (*buf == ch) {
			/* We allocate sufficient size buffers */
			org = (char *) malloc(strlen(buf));
			strcpy(org, &buf[1]);
			snprintf(buf, buf_size, "%d%s",
				 num, org);
			break;
		}
		buf_size--;
	}
}

/*
 * This function does not validate the arguments, does not return
 * errors. Always, used with known values.
 */
static void make_path(monitor_types_t type, char *driver_path,
	              uint32_t sensor_id, char *file_path)
{
	snprintf(file_path, FILEPATHSIZ, "%s/%s", driver_path,
		 filenames[type]);

	str_replace(file_path, FILEPATHSIZ, '#', sensor_id);
}

int read_energy(monitor_types_t type,
		uint32_t sensor_id, uint64_t *pval)
{
	char file_path[FILEPATHSIZ];

	make_path(type, energymon_path, sensor_id, file_path);

	return readfile_u64(file_path, pval);
}

int hsmp_read32(monitor_types_t type,
		uint32_t sensor_id, uint32_t *pval)
{
	char file_path[FILEPATHSIZ];

	make_path(type, hsmpmon_path, sensor_id, file_path);

	return readfile_u32(file_path, pval);
}

int hsmp_write32(monitor_types_t type,
		 uint32_t sensor_id, uint32_t val)
{
	char file_path[FILEPATHSIZ];

	make_path(type, hsmpmon_path, sensor_id, file_path);

	return writefile_u32(file_path, val);
}
