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
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <e_smi/e_smi.h>
#include <e_smi/e_smi_monitor.h>
#include <e_smi/e_smi_utils.h>

/* NODE FILENAMES */
static char energy_file[] = "energy#_input";
static char smu_fw_version_file[] = "smu_firmware_version_raw";
static char hsmp_proto_ver_file[] = "hsmp_protocol_version";
static char socket_power_file[] = "socket#/power";
static char socket_power_limit_file[] = "socket#/power_limit";
static char socket_power_limit_max_file[] = "socket#/power_limit_max";
static char pkg_boostlimit_file[] = "boost_limit";
static char core_boostlimit_file[] = "cpu#/boost_limit";
static char socket_boostlimit_file[] = "socket#/boost_limit";
static char prochot_status_file[] = "socket#/proc_hot";
static char df_pstate_file[] = "socket#/fabric_pstate";
static char fclk_memclk_file[] = "socket#/fabric_clocks_raw";
static char cclk_limit_file[] = "socket#/cclk_limit";
static char socket_c0_residency_file[] = "socket#/c0_residency";
static char ddr_bw_file[] = "ddr_bandwidth_raw";
static char socket_temp_mon_file[] = "socket#/temperature";
static char xgmi_width_file[] = "xgmi_pstate";
static char nbio_pstate_file[] = "nbio_pstate";

static char *filenames[MONITOR_TYPE_MAX] = {energy_file,
					    "",
					    smu_fw_version_file,
					    hsmp_proto_ver_file,
					    socket_power_file,
					    socket_power_limit_file,
					    socket_power_limit_file,
					    socket_power_limit_max_file,
					    core_boostlimit_file,
					    socket_boostlimit_file,
					    core_boostlimit_file,
					    prochot_status_file,
					    xgmi_width_file,
					    df_pstate_file,
					    df_pstate_file,
					    fclk_memclk_file,
					    cclk_limit_file,
					    socket_c0_residency_file,
					    nbio_pstate_file,
					    pkg_boostlimit_file,
					    ddr_bw_file,
					    socket_temp_mon_file,
};

char energymon_path[DRVPATHSIZ], hsmpmon_path[DRVPATHSIZ];

int find_energy(char *devname, char *hwmon_name)
{
	DIR *pdir;
	struct dirent *pdentry;
	char name[FILESIZ];
	FILE *fptr;
	char filepath[FILEPATHSIZ];

	if (!(hwmon_name && devname)) {
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

	if (NULL == path) {
		return EFAULT;
	}
	pdir = opendir(path);
	if (NULL == pdir) {
		return errno;
	}
	while ((pdentry = readdir(pdir))) {
		if (strcmp(pdentry->d_name, HSMP_DEV_NAME) == 0) {
			closedir(pdir);
			return ESMI_SUCCESS;
		}
	}
	closedir(pdir);
	return ENOENT;
}

/*
 * This function does not validate the arguments, does not return
 * errors. Always, used with known values.
 */
static void replace_ch_to_num(char *buf, int buf_size, char ch, unsigned int num)
{
	int tnum = num, i, end, len = 0, digits = 1;
	char *tbuf;

	if (buf == NULL) {
		return;
	}
        while (*buf != '\0') {
		if (*buf == ch) {
			break; // character found
		}
		buf++;
		buf_size--;
	}
	if (*buf == '\0') {
		return; // no character found
	}
	/*
	 * treating number 0 also one digit, so number of digits starts from 1
	 * and ignoring ones position.
	 */
        tnum = tnum / 10;
        while (tnum) { // find if number has more than one digit.
                digits++;
                tnum = tnum / 10;
        }
	/*
	 * If the number has only one digit, just replace the character by
	 * number.
	 */
	if (digits == 1) {
		*buf = 48 + num % 10; // Adding number in place of character
		return;
	}
	/* Finding string length after a specific character */
	tbuf = buf + 1;
	while(*tbuf++ != '\0') {
		len++;
	}
	/*
	 * expected end position of string after adding number in place of
	 * character.
	 */
	end = len + digits;
	/*
	 * If end position crosses buffer size, then adjust the end position
	 * to buffer size and ignore the characters crossing buffer size.
	 */
	if (end >= buf_size) {
		len = len - (end - buf_size) - 1;
		end = buf_size - 1;
	}
	buf[end--] = '\0';
	/*
	 * move the string by (digits - 1)
	 * one digit number can replace on character, hence no need to move.
	 */
	for (i = 0; i < len ; i++, end--) {
		buf[end] = buf[end - digits + 1];
	}
	/*
	 * Ignoring the part of number if it crosses buffer size.
	 */
	while(len < 0) {
		num = num / 10;
		len++;
		digits--;
	}

	for(i = 0; i < digits; i++, end--) {
		buf[end] = 48 + num % 10; // Adding number in place of character
		num = num / 10;
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

	replace_ch_to_num(file_path, FILEPATHSIZ, '#', sensor_id);
}

int read_energy(monitor_types_t type,
		uint32_t sensor_id, uint64_t *pval)
{
	char file_path[FILEPATHSIZ];

	if (NULL == pval) {
		return EFAULT;
	}
	make_path(type, energymon_path, sensor_id, file_path);

	return readsys_u64(file_path, pval);
}

int batch_read_energy(monitor_types_t type, uint64_t *pval, uint32_t entries)
{
	char file_path[FILEPATHSIZ];
	int i, ret, status = 0;

	if (NULL == pval) {
		return EFAULT;
	}
	memset(pval, 0, entries * sizeof(uint64_t));
	for (i = 0; i < entries; i++) {
		make_path(type, energymon_path, i + 1, file_path);
		ret = readsys_u64(file_path, &pval[i]);
		if (ret != 0 && ret != ENODEV) {
			status = ret;
		}
	}

	return status;
}

int hsmp_read64(monitor_types_t type, uint32_t sensor_id, uint64_t *pval)
{
	char file_path[FILEPATHSIZ];

	if (NULL == pval) {
		return EFAULT;
	}
	make_path(type, hsmpmon_path, sensor_id, file_path);

	return readsys_u64(file_path, pval);
}

int hsmp_read32(monitor_types_t type, uint32_t sensor_id, uint32_t *pval)
{
	char file_path[FILEPATHSIZ];

	if (NULL == pval) {
		return EFAULT;
	}
	make_path(type, hsmpmon_path, sensor_id, file_path);

	return readsys_u32(file_path, pval);
}

int hsmp_readstr(monitor_types_t type, uint32_t sensor_id, char *pval, uint32_t len)
{
	char file_path[FILEPATHSIZ];

	if (NULL == pval) {
		return EFAULT;
	}
	make_path(type, hsmpmon_path, sensor_id, file_path);

	return readsys_str(file_path, pval, len);
}

int hsmp_write_s32(monitor_types_t type, uint32_t sensor_id, int32_t val)
{
	char file_path[FILEPATHSIZ];

	make_path(type, hsmpmon_path, sensor_id, file_path);

	return writesys_s32(file_path, val);
}

int hsmp_write32(monitor_types_t type, uint32_t sensor_id, uint32_t val)
{
	char file_path[FILEPATHSIZ];

	make_path(type, hsmpmon_path, sensor_id, file_path);

	return writesys_u32(file_path, val);
}

int hsmp_xfer(struct hsmp_message *msg, int mode)
{
	int fd, ret;

	fd = open(HSMP_CHAR_DEVFILE_NAME, mode);
	if (fd < 0)
		return errno;

	ret = ioctl(fd, HSMP_IOCTL_CMD, msg);
	if (ret)
		ret = errno;

	close(fd);

	return ret;
}
