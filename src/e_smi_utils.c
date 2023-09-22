/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright (c) 2020-2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <e_smi/e_smi_utils.h>

int readsys_u32(char *filepath, uint32_t *pval)
{
	FILE *fptr;

	if (!(filepath && pval)) {
		return EFAULT;
	}
	fptr = fopen(filepath, "r");
	if (fptr == NULL) {
		return errno;
	}
	if (fscanf(fptr, "%u", pval) < 0) {
		fclose(fptr);
		return errno;
	}
	fclose(fptr);
	return 0;
}

int writesys_s32(char *filepath, int32_t val)
{
	FILE *fptr;

	if (NULL == filepath) {
		return EFAULT;
	}
	fptr = fopen(filepath, "w");
	if (fptr == NULL) {
		return errno;
	}
	if (fprintf(fptr, "%d", val) < 0) {
		fclose(fptr);
		return errno;
	}
	fclose(fptr);
	return 0;
}

int writesys_u32(char *filepath, uint32_t val)
{
	FILE *fptr;

	if (NULL == filepath) {
		return EFAULT;
	}
	fptr = fopen(filepath, "w");
	if (fptr == NULL) {
		return errno;
	}
	if (fprintf(fptr, "%u", val) < 0) {
		fclose(fptr);
		return errno;
	}
	fclose(fptr);
	return 0;
}

int readsys_u64(char *filepath, uint64_t *pval)
{
	FILE *fptr;

	if (!(filepath && pval)) {
		return EFAULT;
	}
	fptr = fopen(filepath, "r");
	if (fptr == NULL) {
		return errno;
	}
	if (fscanf(fptr, "%lu", pval) < 0) {
		fclose(fptr);
		return errno;
	}
	fclose(fptr);
	return 0;
}

int readsys_str(char *filepath, char *pval, uint32_t len)
{
	FILE *fptr;

	if (!(filepath && pval)) {
		return EFAULT;
	}
	fptr = fopen(filepath, "r");
	if (fptr == NULL) {
		return errno;
	}
	if (!fgets(pval, len, fptr)) {
		fclose(fptr);
		return errno;
	}
	fclose(fptr);
	return 0;
}

int readmsr_u64(char *filepath, uint64_t *pval, uint64_t reg)
{
	int fd;

	fd = open(filepath, O_RDONLY);
	if (fd < 0)
		return errno;

	if (pread(fd, pval, sizeof(uint64_t), reg) < 0) {
		close(fd);
		return errno;
	}
	close(fd);

	return 0;
}
