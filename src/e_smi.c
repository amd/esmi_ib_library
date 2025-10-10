/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright (c) 2020-2023, Advanced Micro Devices, Inc. All rights reserved.
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

#define HSMP_DRIVER_VERSION_FILE1 "/sys/module/hsmp_common/version"
#define HSMP_DRIVER_VERSION_FILE2 "/sys/module/amd_hsmp/version"
#define MAX_BUFFER_SIZE 256

static struct system_metrics *psm = NULL;

struct cpu_mapping {
	int proc_id;
	int apic_id;
	int sock_id;
};

#define CPU_INFO_LINE_SIZE	1024
#define CPU_INFO_PATH		"/proc/cpuinfo"

static char *delim1 = ":";
static char *delim2 = "\n";
static const char *proc_str = "processor";
static const char *apic_str = "apicid";
static const char *node_str = "physical id";

extern char energymon_path[DRVPATHSIZ];
extern bool *lut;
extern int lut_size;

#define check_sup(x)    ((x >= lut_size) || !lut[x])

#define TU_POS 16
#define ESU_POS 8
#define TU_BITS 4
#define ESU_BITS 5

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

	if (fgets(buf, FILESIZ, fp) == NULL) {
		buf[0] = '\0';
		fclose(fp);
		return -1;
	}

	for (i = 0, j = 0; ((buf[i] != '\0') && (buf[i] != '\n')); i++) {
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
		case ESMI_NO_MSR_DRV:
			return "MSR driver not present";
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
		case ESMI_NO_HSMP_SUP:
			return "HSMP interface not supported/enabled";
		case ESMI_NO_HSMP_MSG_SUP:
			return "HSMP message/command not supported";
		case ESMI_PRE_REQ_NOT_SAT:
			return "Prerequisite to execute the command not satisfied";
		case ESMI_SMU_BUSY:
			return "SMU is busy";
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
		case EINVAL:	return ESMI_INVALID_INPUT;
		case ETIMEDOUT:	return ESMI_HSMP_TIMEOUT;
		case ENOMSG:	return ESMI_NO_HSMP_MSG_SUP;
		case EREMOTEIO:	return ESMI_PRE_REQ_NOT_SAT;
		case EBUSY:	return ESMI_SMU_BUSY;
		default:	return ESMI_UNKNOWN_ERROR;
	}
}

/*
 * Find the amd_energy driver is present and get the
 * path from driver initialzed sysfs path
 */
static esmi_status_t create_amd_energy_monitor(void)
{
	static char hwmon_name[FILESIZ];

	if (find_energy(ENERGY_DEV_NAME, hwmon_name) != 0) {
		return ESMI_NO_ENERGY_DRV;
	}

	snprintf(energymon_path, sizeof(energymon_path), "%s/%s",
		 HWMON_PATH, hwmon_name);

	return ESMI_SUCCESS;
}

static esmi_status_t create_msr_safe_monitor(void)
{
	return errno_to_esmi_status(find_msr_safe());
}

static esmi_status_t create_msr_monitor(void)
{
	return errno_to_esmi_status(find_msr());
}

/*
 * Check whether hsmp character device file exists
 */
static esmi_status_t create_hsmp_monitor()
{
	if (!access(HSMP_CHAR_DEVFILE_NAME, F_OK))
		return ESMI_SUCCESS;

	return ESMI_NO_HSMP_DRV;
}

static void parse_lines(char **str, FILE *fp, int *val, const char *cmp_str)
{
	size_t size = CPU_INFO_LINE_SIZE;
	char *tok;

	while (getline(str, &size, fp) != -1) {
		if ((tok = strtok(*str, delim1)) && (!strncmp(tok, cmp_str, strlen(cmp_str)))) {
			tok  = strtok(NULL, delim2);
			*val = atoi(tok);
			break;
		}
	}
}

static esmi_status_t create_cpu_mappings(struct system_metrics *psm)
{
	size_t size = CPU_INFO_LINE_SIZE;
	int i = 0;
	char *str;
	FILE *fp;
	char *tok;

	str = malloc(CPU_INFO_LINE_SIZE);
	if (!str)
		return ESMI_NO_MEMORY;
	/* If create_cpu_mappings() is called multiple times
	 * dont allocate the memory again.
	 */
	if (!psm->map) {
		psm->map = malloc(psm->total_cores * sizeof(struct cpu_mapping));
		if (!psm->map) {
			free(str);
			return ESMI_NO_MEMORY;
		}
	}

	fp = fopen(CPU_INFO_PATH, "r");
	if (!fp) {
		free(str);
		free(psm->map);
		return ESMI_FILE_ERROR;
	}
	while (getline(&str, &size, fp) != -1) {
		if ((tok = strtok(str, delim1)) && (!strncmp(tok, proc_str, strlen(proc_str)))) {
			tok  = strtok(NULL, delim2);
			psm->map[i].proc_id = atoi(tok);
			parse_lines(&str, fp, &psm->map[i].sock_id, node_str);
			parse_lines(&str, fp, &psm->map[i].apic_id, apic_str);
			i++;
		}
	}

	free(str);

	fclose(fp);

	return ESMI_SUCCESS;
}

static esmi_status_t detect_packages(struct system_metrics *psm)
{
	uint32_t eax, ebx, ecx,edx;
	int max_cores_socket, ret;

	if (NULL == psm) {
		return ESMI_IO_ERROR;
	}
	if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
		return ESMI_IO_ERROR;
	}
	psm->cpu_family = ((eax >> 8) & 0xf) + ((eax >> 20) & 0xff);
	psm->cpu_model = ((eax >> 16) & 0xf) * 0x10 + ((eax >> 4) & 0xf);

	if (!__get_cpuid(0x08000001e, &eax, &ebx, &ecx, &edx)) {
		return ESMI_IO_ERROR;
	}
	psm->threads_per_core = ((ebx >> 8) & 0xff) + 1;

	ret = read_index(CPU_COUNT_PATH);
	if(ret < 0) {
		return ESMI_IO_ERROR;
	}
	psm->total_cores = ret + 1;

	/* fam 0x1A, model0x00-0x1f are dense sockets, Fam 0x1A, Model0x50-0x5F, support more than 255 threads,
	 * On these systems, number of threads is detected by reading
         * Core::X86::Cpuid::SizeId[NC]+1 */
	if (((psm->cpu_family == 0x1A && psm->cpu_model >= 0x10 && psm->cpu_model <= 0x1f)) ||
	    ((psm->cpu_family == 0x1A && psm->cpu_model >= 0x50 && psm->cpu_model <= 0x5F)))
	{
		if (!__get_cpuid(0x80000008, &eax, &ebx, &ecx, &edx))
			return ESMI_IO_ERROR;
		max_cores_socket = (ecx & 0xfff) + 1;
	} else {
		if (!__get_cpuid(0x1, &eax, &ebx, &ecx, &edx))
			return ESMI_IO_ERROR;
		max_cores_socket = ((ebx >> 16) & 0xff);
	}

	/* Number of sockets in the system */
	psm->total_sockets = psm->total_cores / max_cores_socket;

	return ESMI_SUCCESS;
}

static bool check_for_64bit_rapl_reg(struct system_metrics *psysm)
{
	bool ret = true;

	if (psysm->cpu_family == 0x19) {
		switch (psysm->cpu_model) {
			case 0x00 ... 0x0f:
			case 0x30 ... 0x3f:
				ret = false;
				break;
			default:
				ret = true;
				break;
		}
	}

	return ret;
}

static void create_energy_monitor(struct system_metrics *psm)
{
	if (check_for_64bit_rapl_reg(psm)) {
		if (psm->hsmp_status == ESMI_INITIALIZED
		    && psm->hsmp_rapl_reading)
			return;

		if (create_msr_safe_monitor() == ESMI_SUCCESS) {
			psm->msr_safe_status = ESMI_INITIALIZED;
			return;
		}

		if (create_amd_energy_monitor() == ESMI_SUCCESS) {
			psm->energy_status = ESMI_INITIALIZED;
			return;
		}

		if (create_msr_monitor() == ESMI_SUCCESS) {
			psm->msr_status = ESMI_INITIALIZED;
			return;
		}
	} else {
		if (create_amd_energy_monitor() == ESMI_SUCCESS)
			psm->energy_status = ESMI_INITIALIZED;
	}

	return;
}

/*
 * First initialization function to be executed and confirming
 * all the monitor or driver objects should be initialized or not
 */
esmi_status_t esmi_init()
{
	esmi_status_t ret;

	/* esmi_init() ideally should be accompanied with esmi_exit()
	 * but if someone calls it multiple times without esmi_exit()
	 * then, still it should not cause a problem of memory leak.
	 */
	if (!psm) {
		psm = calloc(1, sizeof(*psm));
		if (!psm)
			return ESMI_NO_MEMORY;
	}
	psm->init_status = ESMI_NOT_INITIALIZED;
	psm->energy_status = ESMI_NOT_INITIALIZED;
	psm->msr_status = ESMI_NOT_INITIALIZED;
	psm->msr_safe_status = ESMI_NOT_INITIALIZED;
	psm->hsmp_status = ESMI_NOT_INITIALIZED;

	ret = detect_packages(psm);
	if (ret != ESMI_SUCCESS) {
		return ret;
	}
	if (psm->cpu_family < 0x19)
		return ESMI_NOT_SUPPORTED;

	ret = create_hsmp_monitor();
	if (ret == ESMI_SUCCESS) {
		ret = create_cpu_mappings(psm);
		if (ret != ESMI_SUCCESS)
			return ret;

		struct hsmp_message msg = { 0 };
		msg.msg_id = HSMP_GET_PROTO_VER;
		msg.response_sz = 1;
		msg.sock_ind = 0;
		ret = hsmp_xfer(&msg, O_RDONLY);
		if (ret == ESMI_SUCCESS) {
			psm->hsmp_status = ESMI_INITIALIZED;
			psm->hsmp_proto_ver = msg.args[0];
			init_platform_info(psm);
		}
	}

	create_energy_monitor(psm);

	if (psm->energy_status && psm->msr_status && psm->msr_safe_status && psm->hsmp_status)
		psm->init_status = ESMI_NO_DRV;
	else
		psm->init_status = ESMI_INITIALIZED;

	return psm->init_status;
}

void esmi_exit(void)
{
	if (psm) {
		if (psm->map) {
			free(psm->map);
			psm->map = NULL;
		}
		free(psm);
		psm = NULL;
	}

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
	} else if ((psm->energy_status == ESMI_NOT_INITIALIZED) && \
			(psm->msr_status == ESMI_NOT_INITIALIZED) && \
			(psm->msr_safe_status == ESMI_NOT_INITIALIZED) && \
			(psm->hsmp_status == ESMI_NOT_INITIALIZED || !psm->hsmp_rapl_reading)) {\
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
 * Function to get Rapl units from HSMP mailbox command.
 */
esmi_status_t esmi_rapl_units_hsmp_mailbox_get(uint32_t sock_ind, uint8_t *tu, uint8_t *esu)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_GET_RAPL_UNITS;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (!tu || !esu)
		return ESMI_INVALID_INPUT;

	msg.response_sz	= 1;
	msg.sock_ind	= sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret) {
		*tu = (msg.args[0] >> TU_POS) & (BIT(TU_BITS) - 1);
		*esu = (msg.args[0] >> ESU_POS) & (BIT(ESU_BITS) - 1);
	}

	return errno_to_esmi_status(ret);
}

/*
 * Function to get Rapl core counter from HSMP mailbox command.
 */
esmi_status_t esmi_rapl_core_counter_hsmp_mailbox_get(uint32_t core_ind,
						      uint32_t *counter1, uint32_t *counter0)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_GET_RAPL_CORE_COUNTER;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (!counter0 || !counter1)
		return ESMI_INVALID_INPUT;

	msg.response_sz	= 2;
	msg.num_args	= 1;
	msg.args[0] 	= psm->map[core_ind].apic_id;
	msg.sock_ind	= psm->map[core_ind].sock_id;
	ret = hsmp_xfer(&msg, O_RDWR);
	if (!ret) {
		*counter0 = msg.args[0];
		*counter1 = msg.args[1];
	}

	return errno_to_esmi_status(ret);
}

/*
 * Function to get Rapl package counter from HSMP mailbox command.
 */
esmi_status_t esmi_rapl_package_counter_hsmp_mailbox_get(uint32_t sock_ind,
							 uint32_t *counter1, uint32_t *counter0)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_GET_RAPL_PACKAGE_COUNTER;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (!counter0 || !counter1)
		return ESMI_INVALID_INPUT;

	msg.response_sz	= 2;
	msg.sock_ind	= sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret) {
		*counter0 = msg.args[0];
		*counter1 = msg.args[1];
	}

	return errno_to_esmi_status(ret);
}

/*
 * Function to get core energy from HSMP mailbox commands.
 */
esmi_status_t esmi_core_energy_hsmp_mailbox_get(uint32_t core_ind, uint64_t *penergy)
{
	int ret;
	uint8_t tu, esu;
	uint32_t counter1, counter0;

	if (!penergy)
		return ESMI_INVALID_INPUT;

	ret = esmi_rapl_units_hsmp_mailbox_get(psm->map[core_ind].sock_id, &tu, &esu);
	if (ret)
		return ret;

	ret = esmi_rapl_core_counter_hsmp_mailbox_get(core_ind, &counter1, &counter0);
	if (ret)
		return ret;

	*penergy = (((uint64_t)counter1 << 32) | counter0) * pow(0.5, (double)esu) * 1000000;

	return 0;
}

/*
 * Function to get socket energy from HSMP mailbox commands.
 */
esmi_status_t esmi_package_energy_hsmp_mailbox_get(uint32_t sock_ind, uint64_t *penergy)
{
	int ret;
	uint8_t tu, esu;
	uint32_t counter1, counter0;

	if (!penergy)
		return ESMI_INVALID_INPUT;

	ret = esmi_rapl_units_hsmp_mailbox_get(sock_ind, &tu, &esu);
	if (ret)
		return ret;

	ret = esmi_rapl_package_counter_hsmp_mailbox_get(sock_ind, &counter1, &counter0);
	if (ret)
		return ret;

	*penergy = (((uint64_t)counter1 << 32) | counter0) * pow(0.5, (double)esu) * 1000000;

	return 0;
}

/*
 * Function to get the enenrgy of the core with provided core index
 */
esmi_status_t esmi_core_energy_get(uint32_t core_ind, uint64_t *penergy)
{
	esmi_status_t ret;

	CHECK_ENERGY_GET_INPUT(penergy);
	if (core_ind >= psm->total_cores) {
		return ESMI_INVALID_INPUT;
	}
	core_ind %= psm->total_cores/psm->threads_per_core;

	if (!psm->hsmp_status && psm->hsmp_rapl_reading) {
		return esmi_core_energy_hsmp_mailbox_get(core_ind, penergy);
	} else if (!psm->energy_status) {
		/*
		 * The hwmon enumeration of energy%d_input entries starts
		 * from 1.
		 */
		ret = read_energy_drv(core_ind + 1, penergy);

	} else {
		if (!psm->msr_safe_status)
			ret = read_msr_drv(MSR_SAFE_TYPE, core_ind, penergy, ENERGY_CORE_MSR);
		else
			ret = read_msr_drv(MSR_TYPE, core_ind, penergy, ENERGY_CORE_MSR);
	}

	return errno_to_esmi_status(ret);
}

/*
 * Function to get the enenrgy of the socket with provided socket index
 */
esmi_status_t esmi_socket_energy_get(uint32_t sock_ind, uint64_t *penergy)
{
	esmi_status_t status;
	esmi_status_t ret;
	uint32_t core_ind;

	CHECK_ENERGY_GET_INPUT(penergy);
	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	if (!psm->hsmp_status && psm->hsmp_rapl_reading) {
		return  esmi_package_energy_hsmp_mailbox_get(sock_ind, penergy);
	} else if (!psm->energy_status) {
		/*
		 * The hwmon enumeration of socket energy entries starts
		 * from "total_cores/threads_per_core + sock_ind + 1".
		 */
		ret = read_energy_drv((psm->total_cores/psm->threads_per_core) + sock_ind + 1,
			  penergy);
	} else {
		status = esmi_first_online_core_on_socket(sock_ind, &core_ind);
		if (status != ESMI_SUCCESS)
			return status;
		if (!psm->msr_safe_status)
			ret = read_msr_drv(MSR_SAFE_TYPE, core_ind, penergy, ENERGY_PKG_MSR);
		else
			ret = read_msr_drv(MSR_TYPE, core_ind, penergy, ENERGY_PKG_MSR);
	}

	return errno_to_esmi_status(ret);
}

static int read_all_energy_hsmp_drv(uint64_t *pval, uint32_t cpus)
{
	int i, ret;
	memset(pval, 0, cpus * sizeof(uint64_t));

	for (i = 0; i < cpus; i++) {
		ret = esmi_core_energy_hsmp_mailbox_get(i, &pval[i]);
		if (ret)
			return ret;
	}

	return ret;
}

/*
 * Function to get the enenrgy of cpus and sockets.
 */
esmi_status_t esmi_all_energies_get(uint64_t *penergy)
{
	esmi_status_t ret;
	uint32_t cpus;

	CHECK_ENERGY_GET_INPUT(penergy);
	cpus = psm->total_cores / psm->threads_per_core;

	if (!psm->hsmp_status && psm->hsmp_rapl_reading)
		return read_all_energy_hsmp_drv(penergy, cpus);
	else if (!psm->energy_status)
		ret = batch_read_energy_drv(penergy, cpus);
	else
		ret = batch_read_msr_drv(MSR_TYPE, penergy, cpus);

	return errno_to_esmi_status(ret);
}

/*
 * Function to get the hsmp driver version.
 */
esmi_status_t esmi_hsmp_driver_version_get(struct hsmp_driver_version *hsmp_driver_ver)
{
	FILE *hsmp_driver_ver_file  = NULL;
	FILE *hsmp_driver_ver_file1 = NULL;
	FILE *hsmp_driver_ver_file2 = NULL;
	char line_buffer[MAX_BUFFER_SIZE] = {0};
	char delimiter[] = ".";
	char* token = NULL;

	CHECK_HSMP_GET_INPUT(hsmp_driver_ver);

	hsmp_driver_ver->major = 0;
	hsmp_driver_ver->minor = 0;

	//Open version file
	hsmp_driver_ver_file1 = fopen(HSMP_DRIVER_VERSION_FILE1, "r");
	if(NULL != hsmp_driver_ver_file1) {
		hsmp_driver_ver_file = hsmp_driver_ver_file1;
	} else {
		hsmp_driver_ver_file2 = fopen(HSMP_DRIVER_VERSION_FILE2, "r");
		if(NULL != hsmp_driver_ver_file2) {
			hsmp_driver_ver_file = hsmp_driver_ver_file2;
		} else {
			return ESMI_FILE_NOT_FOUND;
		}
	}

	//Read first line from version file, which will have the version number
	if(!fgets(line_buffer, MAX_BUFFER_SIZE, hsmp_driver_ver_file)) {
		return ESMI_FILE_ERROR;
	}

	//Fetch major version
	token = strtok(line_buffer, delimiter);
	if(token) {
		hsmp_driver_ver->major = atoi(token);
	}

	//Fetch minor version
	token = strtok(NULL, delimiter);
	if(token) {
		hsmp_driver_ver->minor = atoi(token);
	}

	fclose(hsmp_driver_ver_file);

	return ESMI_SUCCESS;
}

esmi_status_t esmi_smu_fw_version_get(struct smu_fw_version *smu_fw)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id = HSMP_GET_SMU_VER;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(smu_fw);

	msg.response_sz = 1;
	msg.sock_ind = 0;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*(uint32_t *)smu_fw = msg.args[0];

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
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id = HSMP_GET_SOCKET_POWER;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(ppower);

	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	msg.response_sz = 1;
	msg.sock_ind = sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*ppower = msg.args[0];

	return errno_to_esmi_status(ret);
}

/*
 * Function to get the Power Limit of the Socket with provided
 * socket index
 */
esmi_status_t esmi_socket_power_cap_get(uint32_t sock_ind, uint32_t *pcap)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id = HSMP_GET_SOCKET_POWER_LIMIT;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(pcap);

	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	msg.response_sz = 1;
	msg.sock_ind = sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*pcap = msg.args[0];

	return errno_to_esmi_status(ret);
}

/*
 * Function to get the Maximum Power Limit of the Socket with provided
 * socket index
 */
esmi_status_t esmi_socket_power_cap_max_get(uint32_t sock_ind, uint32_t *pmax)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id = HSMP_GET_SOCKET_POWER_LIMIT_MAX;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(pmax);

	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	msg.response_sz = 1;
	msg.sock_ind = sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*pmax = msg.args[0];

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
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id = HSMP_SET_SOCKET_POWER_LIMIT;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	/* TODO check against minimum limit */
	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	msg.num_args = 1;
	msg.sock_ind = sock_ind;
	msg.args[0] = cap;
	ret = hsmp_xfer(&msg, O_WRONLY);

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
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id = HSMP_GET_BOOST_LIMIT;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(pboostlimit);

	if (core_ind >= psm->total_cores) {
		return ESMI_INVALID_INPUT;
	}

	if (!psm->map)
		return ESMI_IO_ERROR;

	msg.num_args = 1;
	msg.response_sz = 1;
	msg.sock_ind = psm->map[core_ind].sock_id;
	msg.args[0] = psm->map[core_ind].apic_id;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*pboostlimit = msg.args[0];

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
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id = HSMP_SET_BOOST_LIMIT;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (core_ind >= psm->total_cores) {
		return ESMI_INVALID_INPUT;
	}

	if (boostlimit > UINT16_MAX)
		return ESMI_INVALID_INPUT;

	if (!psm->map)
		return ESMI_IO_ERROR;

	msg.num_args = 1;
	msg.sock_ind = psm->map[core_ind].sock_id;
	msg.args[0] = (psm->map[core_ind].apic_id << 16) | boostlimit;
	ret = hsmp_xfer(&msg, O_WRONLY);

	return errno_to_esmi_status(ret);
}

/*
 * Function to Set or Control the freq limits of the socket
 * with provided socket index and boostlimit value to be set
 */
esmi_status_t esmi_socket_boostlimit_set(uint32_t sock_ind,
					 uint32_t boostlimit)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id = HSMP_SET_BOOST_LIMIT_SOCKET;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	/* TODO check boostlimit against a valid range */
	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	if (boostlimit > UINT16_MAX)
		return ESMI_INVALID_INPUT;

	msg.num_args = 1;
	msg.sock_ind = sock_ind;
	msg.args[0] = boostlimit;
	ret = hsmp_xfer(&msg, O_WRONLY);

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_prochot_status_get(uint32_t sock_ind, uint32_t *prochot)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id = HSMP_GET_PROC_HOT;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(prochot);

	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	msg.response_sz = 1;
	msg.sock_ind = sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*prochot = msg.args[0];

	return errno_to_esmi_status(ret);
}

/* xgmi link is used in multi socket system
 * width can be set to 2/8/16 lanes
 */
esmi_status_t esmi_xgmi_width_set(uint8_t sock_ind, uint8_t min, uint8_t max)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;
	uint16_t width;

	CHECK_HSMP_INPUT();

	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	width = (min << 8) | max;
	msg.msg_id = HSMP_SET_XGMI_LINK_WIDTH;
	msg.num_args = 1;
	msg.args[0] = width;
	msg.sock_ind = sock_ind;
	ret = hsmp_xfer(&msg, O_WRONLY);
	if (ret)
		return errno_to_esmi_status(ret);

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_xgmi_width_get(uint32_t sock_ind, uint8_t* min, uint8_t* max)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	CHECK_HSMP_INPUT();

	if (!(min && max))
		return ESMI_ARG_PTR_NULL;

	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	msg.msg_id = HSMP_SET_XGMI_LINK_WIDTH;
	msg.num_args = 1;
	msg.response_sz = 1;
	msg.args[0] = BIT(31);
	msg.sock_ind = sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
	{
		*max = msg.args[0] & 0xFF;
		*min = (msg.args[0] >> 8) & 0xFF;
	}

	return errno_to_esmi_status(ret);
}

/* enable APB, no arguments */
esmi_status_t esmi_apb_enable(uint32_t sock_ind)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id = HSMP_SET_AUTO_DF_PSTATE;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	/*
	 * While the socket is in PC6 or if PROCHOT_L is
	 * asserted, the lowest DF P-state (highest value) is enforced
	 * regardless of the APBEnable/APBDisable
	 */
	msg.sock_ind = sock_ind;
	ret = hsmp_xfer(&msg, O_WRONLY);

	return errno_to_esmi_status(ret);
}

/* disable APB, user can set 0 ~ 3 */
esmi_status_t esmi_apb_disable(uint32_t sock_ind, uint8_t pstate)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id = HSMP_SET_DF_PSTATE;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args = 1;
	msg.sock_ind = sock_ind;
	msg.args[0] = pstate;
	ret = hsmp_xfer(&msg, O_WRONLY);

	return errno_to_esmi_status(ret);
}

/* APB disable get */
esmi_status_t esmi_apb_status_get(uint32_t sock_ind, uint8_t* apb_disabled, uint8_t* pstate)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id = HSMP_SET_DF_PSTATE;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (!(apb_disabled && pstate))
		return ESMI_ARG_PTR_NULL;

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args = 1;
	msg.response_sz = 1;
	msg.sock_ind = sock_ind;
	msg.args[0] = BIT(31);
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret) {
		*apb_disabled = (msg.args[0] >> 8) & 0x1;
		if(1 == *apb_disabled)
			*pstate = msg.args[0] & 0xFF;
		else
			*pstate = 0xFF;
	}

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_fclk_mclk_get(uint32_t sock_ind,
				 uint32_t *fclk, uint32_t *mclk)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id = HSMP_GET_FCLK_MCLK;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

        if (!(fclk && mclk))
                return ESMI_ARG_PTR_NULL;
	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.response_sz = 2;
	msg.sock_ind = sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret) {
		*fclk = msg.args[0];
		*mclk = msg.args[1];
	}

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_cclk_limit_get(uint32_t sock_ind, uint32_t *cclk)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id = HSMP_GET_CCLK_THROTTLE_LIMIT;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(cclk);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.response_sz = 1;
	msg.sock_ind = sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*cclk = msg.args[0];

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
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id = HSMP_GET_C0_PERCENT;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(pc0_residency);
	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	msg.response_sz = 1;
	msg.sock_ind = sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*pc0_residency = msg.args[0];

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_socket_lclk_dpm_level_set(uint32_t sock_ind, uint8_t nbio_id,
					     uint8_t min, uint8_t max)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;
	uint32_t dpm_val;

	msg.msg_id = HSMP_SET_NBIO_DPM_LEVEL;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	dpm_val = (nbio_id << 16) | (max << 8) | min;

	msg.num_args = 1;
	msg.sock_ind = sock_ind;
	msg.args[0] = dpm_val;
	ret = hsmp_xfer(&msg, O_WRONLY);

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_socket_lclk_dpm_level_get(uint8_t sock_ind, uint8_t nbio_id,
					     struct dpm_level *dpm)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_GET_NBIO_DPM_LEVEL;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(dpm);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.response_sz	= 1;
	msg.sock_ind	= sock_ind;
	msg.args[0]	= nbio_id << 16;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret) {
		dpm->max_dpm_level = msg.args[0] >> 8;
		dpm->min_dpm_level = msg.args[0];
	}

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_ddr_bw_get(uint8_t sock_ind, struct ddr_bw_metrics *ddr_bw)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;
	uint32_t bw;

	msg.msg_id = HSMP_GET_DDR_BANDWIDTH;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(ddr_bw);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.response_sz = 1;
	msg.sock_ind = sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (ret)
		return errno_to_esmi_status(ret);
	bw = msg.args[0];

	ddr_bw->max_bw = bw >> 20;
	ddr_bw->utilized_bw = (bw >> 8) & 0xFFF;
	ddr_bw->utilized_pct = bw & 0xFF;

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_socket_temperature_get(uint32_t sock_ind, uint32_t *ptmon)
{
	struct hsmp_message msg = { 0 };
	uint32_t int_part, fract_part;
	esmi_status_t ret;

	msg.msg_id = HSMP_GET_TEMP_MONITOR;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	CHECK_HSMP_GET_INPUT(ptmon);

	msg.response_sz = 1;
	msg.sock_ind = sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret) {
		/* convert temperature to milli degree celsius */
		int_part = ((msg.args[0] >> 8) & 0xFF) * 1000;
		fract_part = ((msg.args[0] >> 5) & 0x7) * 125;
		*ptmon = int_part + fract_part;
	}

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_dimm_temp_range_and_refresh_rate_get(uint8_t sock_ind, uint8_t dimm_addr,
							struct temp_range_refresh_rate *rate)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_GET_DIMM_TEMP_RANGE;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	CHECK_HSMP_GET_INPUT(rate);

	msg.response_sz	= 1;
	msg.num_args	= 1;
	msg.args[0]	= dimm_addr;
	msg.sock_ind	= sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret) {
		rate->range = msg.args[0];
		rate->ref_rate = msg.args[0] >> 3;
	}

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_dimm_power_consumption_get(uint8_t sock_ind, uint8_t dimm_addr,
					      struct dimm_power *dimm_pow)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_GET_DIMM_POWER;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	CHECK_HSMP_GET_INPUT(dimm_pow);

	msg.response_sz	= 1;
	msg.num_args	= 1;
	msg.args[0]	= dimm_addr;
	msg.sock_ind	= sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret) {
		dimm_pow->power = msg.args[0] >> 17;
		dimm_pow->update_rate = msg.args[0] >> 8;
		dimm_pow->dimm_addr = msg.args[0];
	}

	return errno_to_esmi_status(ret);
}

#define SCALING_FACTOR  0.25

static void decode_dimm_temp(uint16_t raw, float *temp)
{
	if (raw <= 0x3FF)
		*temp = raw * SCALING_FACTOR;
	else
		*temp = (raw - 0x800) * SCALING_FACTOR;
}

esmi_status_t esmi_dimm_thermal_sensor_get(uint8_t sock_ind, uint8_t dimm_addr,
					   struct dimm_thermal *dimm_temp)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_GET_DIMM_THERMAL;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	CHECK_HSMP_GET_INPUT(dimm_temp);

	msg.response_sz = 1;
	msg.num_args	= 1;
	msg.args[0]	= dimm_addr;
	msg.sock_ind	= sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret) {
		dimm_temp->sensor = msg.args[0] >> 21;
		dimm_temp->update_rate = msg.args[0] >> 8;
		dimm_temp->dimm_addr = msg.args[0];
		decode_dimm_temp(dimm_temp->sensor, &dimm_temp->temp);
	}

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_socket_current_active_freq_limit_get(uint32_t sock_ind, uint16_t *freq,
							char **src_type)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;
	uint8_t src_len;
	uint16_t limit;
	uint8_t index = 0;
	uint8_t ind = 0;

	msg.msg_id	= HSMP_GET_SOCKET_FREQ_LIMIT;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (freq == NULL || src_type == NULL)
		return ESMI_INVALID_INPUT;

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	/* frequency limit source names array length */
	src_len = ARRAY_SIZE(freqlimitsrcnames);

	msg.response_sz = 1;
	msg.sock_ind	= sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (ret)
		return errno_to_esmi_status(ret);

	*freq = msg.args[0] >> 16;
	limit = msg.args[0] & 0xFFFF;

	while (limit != 0 && index < src_len) {
		if ((limit & 1) == 1) {
			src_type[ind] = freqlimitsrcnames[index];
			ind++;
		}
		index += 1;
		limit = limit >> 1;
	}

	return ESMI_SUCCESS;
}

esmi_status_t esmi_current_freq_limit_core_get(uint32_t core_id, uint32_t *freq)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_GET_CCLK_CORE_LIMIT;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(freq);

	if (core_id >= psm->total_cores)
		return ESMI_INVALID_INPUT;

	if (!psm->map)
		return ESMI_IO_ERROR;

	msg.num_args	= 1;
	msg.response_sz = 1;
	msg.args[0]	= psm->map[core_id].apic_id;
	msg.sock_ind	= psm->map[core_id].sock_id;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*freq = msg.args[0];

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_pwr_svi_telemetry_all_rails_get(uint32_t sock_ind, uint32_t *power)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_GET_RAILS_SVI;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(power);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.response_sz	= 1;
	msg.sock_ind	= sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*power = msg.args[0];

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_socket_freq_range_get(uint8_t sock_ind, uint16_t *fmax, uint16_t *fmin)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_GET_SOCKET_FMAX_FMIN;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	if (!fmax || !fmin)
		return ESMI_INVALID_INPUT;

	CHECK_HSMP_INPUT();

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.response_sz	= 1;
	msg.sock_ind	= sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret) {
		*fmax = msg.args[0] >> 16;
		*fmin = msg.args[0] & 0xFFFF;
	}

	return errno_to_esmi_status(ret);
}

static int validate_link_name(char *name, int *encode_val)
{
	int i = 0;

	if (!psm->lencode)
		return ESMI_NO_HSMP_MSG_SUP;
	if (!name)
		return ESMI_ARG_PTR_NULL;
	while (psm->lencode[i].name != NULL) {
		if (!strcmp(name, psm->lencode[i].name)) {
			*encode_val = psm->lencode[i].val;
			return ESMI_SUCCESS;
		}
		i++;
	}
	return ESMI_INVALID_INPUT;
}

static esmi_status_t validate_bw_type(uint8_t bw_type)
{
	esmi_status_t ret;

	switch (bw_type) {
		case AGG_BW:
		case RD_BW:
		case WR_BW:
			ret = ESMI_SUCCESS;
			break;
		default:
			ret = ESMI_INVALID_INPUT;
	};

	return ret;
}


esmi_status_t esmi_current_io_bandwidth_get(uint8_t sock_ind, struct link_id_bw_type link,
					    uint32_t *io_bw)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;
	int encode_val = 0;

	msg.msg_id	= HSMP_GET_IOLINK_BANDWITH;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(io_bw);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	if(validate_link_name(link.link_name, &encode_val))
		return ESMI_INVALID_INPUT;

	msg.response_sz = 1;
	msg.num_args	= 1;
	msg.args[0]	= link.bw_type | encode_val << 8;
	msg.sock_ind	= sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*io_bw = msg.args[0];

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_current_xgmi_bw_get(uint8_t sock_ind, struct link_id_bw_type link,
				       uint32_t *xgmi_bw)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;
	int encode_val = 0;

	msg.msg_id	= HSMP_GET_XGMI_BANDWITH;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(xgmi_bw);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	if(validate_link_name(link.link_name, &encode_val))
		return ESMI_INVALID_INPUT;

	if (validate_bw_type(link.bw_type))
		return ESMI_INVALID_INPUT;

	msg.response_sz = 1;
	msg.num_args	= 1;
	msg.args[0]	= link.bw_type | encode_val << 8;
	msg.sock_ind	= sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*xgmi_bw = msg.args[0];

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_gmi3_link_width_range_set(uint8_t sock_ind, uint8_t min_link_width,
					     uint8_t max_link_width)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_SET_GMI3_WIDTH;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.sock_ind	= sock_ind;
	msg.args[0]	= (min_link_width << 8) | max_link_width;
	ret = hsmp_xfer(&msg, O_WRONLY);

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_pcie_link_rate_set(uint8_t sock_ind, uint8_t rate_ctrl, uint8_t *prev_mode)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_SET_PCI_RATE;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(prev_mode);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.response_sz	= 1;
	msg.sock_ind	= sock_ind;
	msg.args[0]	= rate_ctrl;
	ret = hsmp_xfer(&msg, O_RDWR);
	if (!ret)
		/* 0x3 is the mask for prev mode(2 bits) */
		*prev_mode = msg.args[0] & 0x3;

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_pwr_efficiency_mode_set(uint8_t sock_ind, uint8_t mode)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_SET_POWER_MODE;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.sock_ind	= sock_ind;
	msg.args[0]	= mode;
	ret = hsmp_xfer(&msg, O_RDWR);

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_pwr_efficiency_mode_get(uint8_t sock_ind, uint8_t *mode)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_SET_POWER_MODE;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(mode);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.response_sz = 1;
	msg.sock_ind	= sock_ind;
	msg.args[0]	= BIT(31);
	ret = hsmp_xfer(&msg, O_RDONLY);
	/* bits 0-2 contain current mode */
	if (!ret)
		*mode =  msg.args[0] & (BIT(3) - 1);

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_df_pstate_range_set(uint8_t sock_ind, uint8_t min_pstate,
				       uint8_t max_pstate)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_SET_PSTATE_MAX_MIN;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.sock_ind	= sock_ind;
	msg.args[0]	= (min_pstate << 8) | max_pstate;
	ret = hsmp_xfer(&msg, O_WRONLY);

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_df_pstate_range_get(uint8_t sock_ind, uint8_t *min_pstate,
				       uint8_t *max_pstate)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_SET_PSTATE_MAX_MIN;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (!(max_pstate && min_pstate))
		return ESMI_ARG_PTR_NULL;

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.response_sz	= 1;
	msg.sock_ind	= sock_ind;
	msg.args[0] = BIT(31);
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
	{
		*max_pstate = msg.args[0] & 0xFF;
		*min_pstate = (msg.args[0] >> 8) & 0xFF;
	}

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_hsmp_proto_ver_get(uint32_t *proto_ver)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	CHECK_HSMP_GET_INPUT(proto_ver);

	if (psm->hsmp_proto_ver) {
		*proto_ver = psm->hsmp_proto_ver;
		return ESMI_SUCCESS;
	}

	msg.msg_id = HSMP_GET_PROTO_VER;

	msg.response_sz = 1;
	msg.sock_ind = 0;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*proto_ver = msg.args[0];

	return errno_to_esmi_status(ret);
}

/*
 * To get the version number of the metrics table
 */
esmi_status_t esmi_metrics_table_version_get(uint32_t *metrics_version)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_GET_METRIC_TABLE_VER;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(metrics_version);

	msg.response_sz	= 1;
	msg.sock_ind	= 0;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*metrics_version = msg.args[0];

	return errno_to_esmi_status(ret);
}

/*
 * To get the metrics table
 */
esmi_status_t esmi_metrics_table_get(uint8_t sock_ind, struct hsmp_metric_table *metrics_table)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret = 0;
	char filepath[FILEPATHSIZ];
	FILE *fp;
	int num;

	msg.msg_id	= HSMP_GET_METRIC_TABLE;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	snprintf(filepath, FILEPATHSIZ,
		 "%s/socket%d/metrics_bin",
		 HSMP_METRICTABLE_PATH, sock_ind);

	fp = fopen(filepath, "rb");

	if(fp == NULL)
		return ESMI_FILE_ERROR;

	num = fread(metrics_table, sizeof(struct hsmp_metric_table), 1, fp);
	if (num != 1) {
		perror("error reading file");
		ret = ferror(fp);
	}

	fclose(fp);
	return errno_to_esmi_status(ret);
}

/*
 * To get the the dram address of the metrics table
 */
esmi_status_t esmi_dram_address_metrics_table_get(uint8_t sock_ind, uint64_t *dram_addr)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_GET_METRIC_TABLE_DRAM_ADDR;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

    if (!dram_addr)
		return ESMI_ARG_PTR_NULL;
    if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.response_sz	= 2;
	msg.sock_ind	= sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*dram_addr = msg.args[0] | (((uint64_t)(msg.args[1])) << 32);

	return errno_to_esmi_status(ret);
}

/*
 * Function to test the HSMP interface.
 */
esmi_status_t esmi_test_hsmp_mailbox(uint8_t sock_ind, uint32_t *data)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_TEST;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(data);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.response_sz = 1;
	msg.num_args	= 1;
	msg.sock_ind	= sock_ind;
	msg.args[0]	= *data;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*data = msg.args[0];

	return errno_to_esmi_status(ret);
}

/*
 * Function to set CpuRailIsoFreqPolicy.
 */
esmi_status_t esmi_cpurail_isofreq_policy_set(uint8_t sock_ind, bool *val)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_CPU_RAIL_ISO_FREQ_POLICY;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(val);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.response_sz = 1;
	msg.sock_ind	= sock_ind;
	msg.args[0] 	= *val;

	ret = hsmp_xfer(&msg, O_RDWR);
	if (!ret)
		*val = msg.args[0] & BIT(0);

	return errno_to_esmi_status(ret);
}

/*
 * Function to get CpuRailIsoFreqPolicy.
 */
esmi_status_t esmi_cpurail_isofreq_policy_get(uint8_t sock_ind, bool *val)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_CPU_RAIL_ISO_FREQ_POLICY;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(val);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.response_sz = 1;
	msg.sock_ind	= sock_ind;
	msg.args[0] 	= BIT(31);

	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*val = msg.args[0] & BIT(0);

	return errno_to_esmi_status(ret);
}

/*
 * Function to enable/disable DF C-state.
 */
esmi_status_t esmi_dfc_enable_set(uint8_t sock_ind, bool *val)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_DFC_ENABLE_CTRL;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(val);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.sock_ind	= sock_ind;
	msg.args[0]	= *val;
	msg.response_sz = 1;

	ret = hsmp_xfer(&msg, O_RDWR);
	if (!ret)
		*val = msg.args[0] & BIT(0);

	return errno_to_esmi_status(ret);
}

/*
 * Function to get DF C-state status.
 */
esmi_status_t esmi_dfc_ctrl_setting_get(uint8_t sock_ind, bool *val)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_DFC_ENABLE_CTRL;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(val);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.sock_ind	= sock_ind;
	msg.args[0]	= BIT(31);
	msg.response_sz = 1;

	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*val = msg.args[0] & BIT(0);

	return errno_to_esmi_status(ret);
}

/*
 * Function to set xGMI P-state range.
 */
esmi_status_t esmi_xgmi_pstate_range_set(uint8_t sock_ind, uint8_t min_state, uint8_t max_state)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_SET_XGMI_PSTATE_RANGE;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.sock_ind	= sock_ind;
	msg.args[0]	= min_state << 8 | max_state;
	ret = hsmp_xfer(&msg, O_WRONLY);

	return errno_to_esmi_status(ret);
}

/*
 * Function to get xGMI P-state range.
 */
esmi_status_t esmi_xgmi_pstate_range_get(uint8_t sock_ind, uint8_t *min_state, uint8_t *max_state)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_SET_XGMI_PSTATE_RANGE;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (!(min_state && max_state))
		return ESMI_ARG_PTR_NULL;

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.response_sz = 1;
	msg.args[0] = BIT(31);
	msg.sock_ind	= sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if(!ret)
	{
		*max_state = msg.args[0] & 0xFF;
		*min_state = (msg.args[0] >> 8) & 0xFF;
	}

	return errno_to_esmi_status(ret);
}

/*
 * Function to set PC6 Enable.
 */
esmi_status_t esmi_pc6_enable_set(uint8_t sock_ind, uint8_t pc6_enable)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_PC6_ENABLE;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	if ((pc6_enable != 0) && (pc6_enable != 1))
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.args[0]	= pc6_enable;
	msg.sock_ind	= sock_ind;
	ret = hsmp_xfer(&msg, O_WRONLY);

	return errno_to_esmi_status(ret);
}

/*
 * Function to get PC6 Enable.
 */
esmi_status_t esmi_pc6_enable_get(uint8_t sock_ind, uint8_t *current_pc6_enable)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_PC6_ENABLE;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(current_pc6_enable);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.response_sz = 1;
	msg.args[0] = BIT(31);
	msg.sock_ind	= sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if(!ret)
		*current_pc6_enable = msg.args[0] & 0x1;

	return errno_to_esmi_status(ret);
}

/*
 * Function to set CC6 Enable.
 */
esmi_status_t esmi_cc6_enable_set(uint8_t sock_ind, uint8_t cc6_enable)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_CC6_ENABLE;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	if ((cc6_enable != 0) && (cc6_enable != 1))
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.args[0]	= cc6_enable;
	msg.sock_ind	= sock_ind;
	ret = hsmp_xfer(&msg, O_WRONLY);

	return errno_to_esmi_status(ret);
}

/*
 * Function to get CC6 Enable.
 */
esmi_status_t esmi_cc6_enable_get(uint8_t sock_ind, uint8_t *current_cc6_enable)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_CC6_ENABLE;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(current_cc6_enable);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.response_sz = 1;
	msg.args[0] = BIT(31);
	msg.sock_ind	= sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if(!ret)
		*current_cc6_enable = msg.args[0] & 0x1;

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_dimm_sb_reg_read(uint8_t sock_ind, struct dimm_sb_info *inout)
{
	struct hsmp_message msg = {};
	esmi_status_t ret;

	msg.msg_id	= HSMP_DIMM_SB_RD;

	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(inout);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.response_sz = 1;
	msg.args[0] = inout->m_dimm_sb_info_inarg.reg_value;
	msg.sock_ind	= sock_ind;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if(!ret)
		inout->data = msg.args[0];

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_read_ccd_power(uint32_t coreid, uint32_t *power)
{
	struct hsmp_message msg = {};
	esmi_status_t ret;

	msg.msg_id	= HSMP_READ_CCD_POWER;

	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(power);

	if (coreid >= psm->total_cores)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.response_sz = 1;
	msg.args[0] = psm->map[coreid].apic_id;
	msg.sock_ind	= psm->map[coreid].sock_id;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if(!ret)
		*power = msg.args[0];
	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_read_tdelta(uint8_t sock_ind, uint8_t *status)
{
	struct hsmp_message msg = {};
	esmi_status_t ret;

	msg.msg_id	= HSMP_READ_TDELTA;

	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(status);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.response_sz = 1;
	msg.sock_ind	= sock_ind;

	ret = hsmp_xfer(&msg, O_RDONLY);
	if(!ret)
		*status = msg.args[0];
	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_get_svi3_vr_controller_temp(uint8_t sock_ind, struct svi3_info *inout)
{
	struct hsmp_message msg = {};
	esmi_status_t ret;

	msg.msg_id	= HSMP_GET_SVI3_VR_CTRL_TEMP;

	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(inout);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	msg.response_sz = 1;
	msg.num_args	= 1;
	msg.sock_ind	= sock_ind;
	msg.args[0]		= inout->m_svi3_info_inarg.reg_value;

	ret = hsmp_xfer(&msg, O_RDONLY);
	if(!ret) {
		svi3_getinfo_outarg svi3_getinfo_outarg_temp;
		svi3_getinfo_outarg_temp.reg_value = msg.args[0];

		inout->m_svi3_info_inarg.info.svi3_rail_index = svi3_getinfo_outarg_temp.info.svi3_rail_index;
		inout->m_svi3_info_inarg.info.svi3_temperature = svi3_getinfo_outarg_temp.info.svi3_temperature;
	}

	return errno_to_esmi_status(ret);
}
