/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright (c) 2020-2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 */
#include <dirent.h>
#include <errno.h>
#include <math.h>
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

char energymon_path[DRVPATHSIZ];

static uint64_t energy_unit = 0;

/* NODE FILENAMES */
static char energy_file[] = "energy#_input";
static char msr_safe_file[] = "#/msr_safe";
static char msr_file[] = "#/msr";

static char *filenames[MONITOR_TYPE_MAX] = { energy_file,
					     msr_safe_file,
					     msr_file
};

int find_energy(char *devname, char *hwmon_name)
{
	char *c;
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
			if (fgets(name, FILESIZ, fptr) == NULL) {
				name[0] = '\0';
			}
			if ((c = strchr(name, '\n')) != NULL )
				*c = '\0';
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

int find_msr_safe()
{
	char file_path[FILEPATHSIZ];
	int ret;

	make_path(MSR_SAFE_TYPE, MSR_PATH, 0, file_path);
	ret = access(file_path, F_OK);
	if (ret == -1)
		return errno;

	return ret;
}

int find_msr()
{
	char file_path[FILEPATHSIZ];
	int ret;

	make_path(MSR_TYPE, MSR_PATH, 0, file_path);
	ret = access(file_path, F_OK);
	if (ret == -1)
		return errno;

	return ret;
}

static int read_energy_unit(monitor_types_t type)
{
	char file_path[FILEPATHSIZ];
	int ret;

	make_path(type, MSR_PATH, 0, file_path);
	ret = readmsr_u64(file_path, &energy_unit, ENERGY_PWR_UNIT_MSR);
	if (ret)
		return ret;

	energy_unit = (energy_unit & AMD_ENERGY_UNIT_MASK) >> AMD_ENERGY_UNIT_OFFSET;

	return ESMI_SUCCESS;
}

int read_energy_drv(uint32_t sensor_id, uint64_t *pval)
{
	char file_path[FILEPATHSIZ];

	if (NULL == pval) {
		return EFAULT;
	}
	make_path(ENERGY_TYPE, energymon_path, sensor_id, file_path);

	return readsys_u64(file_path, pval);
}

int read_msr_drv(monitor_types_t type, uint32_t sensor_id, uint64_t *pval, uint64_t reg)
{
        int ret;
        char file_path[FILEPATHSIZ];

        *pval = 0;

	if (!energy_unit){
		ret = read_energy_unit(type);
		if (ret)
			return ret;
	}
        make_path(type, MSR_PATH, sensor_id, file_path);
        ret = readmsr_u64(file_path, pval, reg);

        *pval = *pval * pow(0.5, (double)energy_unit) * 1000000;
        return ret;
}

int batch_read_energy_drv(uint64_t *pval, uint32_t cpus)
{
	char file_path[FILEPATHSIZ];
	int i, ret, status = 0;

	if (NULL == pval) {
		return EFAULT;
	}
	memset(pval, 0, cpus * sizeof(uint64_t));
	for (i = 0; i < cpus; i++) {
		make_path(ENERGY_TYPE, energymon_path, i + 1, file_path);
		ret = readsys_u64(file_path, &pval[i]);
		if (ret != 0 && ret != ENODEV) {
			status = ret;
		}
	}

	return status;
}

int batch_read_msr_drv(monitor_types_t type, uint64_t *pval, uint32_t cpus)
{
	char file_path[FILEPATHSIZ];
	int i, ret;

	if (!energy_unit){
		ret = read_energy_unit(type);
		if (ret)
			return ret;
	}
	memset(pval, 0, cpus * sizeof(uint64_t));
	for (i = 0; i < cpus; i++) {
		make_path(type, MSR_PATH, i, file_path);
		ret = readmsr_u64(file_path, &pval[i], ENERGY_CORE_MSR);
		if (ret != 0 && ret != ENODEV)
			return ret;

		pval[i] = pval[i] * pow(0.5, (double)energy_unit) * 1000000;
	}
	return ret;
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
