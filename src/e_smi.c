/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright (c) 2020-2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 */
#include <cpuid.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>

#include <e_smi/e_smi.h>
#include <e_smi/e_smi_monitor.h>

static const struct system_metrics *psm = NULL;

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

static esmi_status_t create_msr_monitor(void)
{
	return errno_to_esmi_status(find_msr_safe());
}

/*
 * Check whether hsmp character device file exists
 */
static esmi_status_t create_hsmp_monitor(struct system_metrics *sm)
{
	if (!access(HSMP_CHAR_DEVFILE_NAME, F_OK))
		return ESMI_SUCCESS;

	return ESMI_NO_HSMP_DRV;
}

static void parse_lines(char **str, FILE *fp, uint32_t *val, const char *cmp_str)
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

static esmi_status_t create_cpu_mappings(struct system_metrics *sm)
{
	size_t size = CPU_INFO_LINE_SIZE;
	int i = 0;
	char *str;
	FILE *fp;
	char *tok;

	str = malloc(CPU_INFO_LINE_SIZE);
	if (!str)
		return ESMI_NO_MEMORY;

	sm->map = malloc(sm->total_cores * sizeof(struct cpu_mapping));
	if (!sm->map) {
		free(str);
		return ESMI_NO_MEMORY;
	}

	fp = fopen(CPU_INFO_PATH, "r");
	if (!fp) {
		free(str);
		free(sm->map);
		return ESMI_FILE_ERROR;
	}
	while (getline(&str, &size, fp) != -1) {
		if ((tok = strtok(str, delim1)) && (!strncmp(tok, proc_str, strlen(proc_str)))) {
			tok  = strtok(NULL, delim2);
			sm->map[i].proc_id = atoi(tok);
			parse_lines(&str, fp, &sm->map[i].sock_id, node_str);
			parse_lines(&str, fp, &sm->map[i].apic_id, apic_str);
			i++;
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
	sm.msr_status = ESMI_NOT_INITIALIZED;
	sm.hsmp_status = ESMI_NOT_INITIALIZED;

	ret = detect_packages(&sm);
	if (ret != ESMI_SUCCESS) {
		return ret;
	}
	if (sm.cpu_family < 0x19)
		return ESMI_NOT_SUPPORTED;

	if (check_for_64bit_rapl_reg(&sm)) {
		ret = create_msr_monitor();
		if (ret == ESMI_SUCCESS)
			sm.msr_status = ESMI_INITIALIZED;
	} else {
		ret = create_energy_monitor();
		if (ret == ESMI_SUCCESS)
			sm.energy_status = ESMI_INITIALIZED;
	}
	ret = create_hsmp_monitor(&sm);
	if (ret == ESMI_SUCCESS) {
		ret = create_cpu_mappings(&sm);
		if (ret != ESMI_SUCCESS)
			return ret;

		struct hsmp_message msg = { 0 };
		msg.msg_id = HSMP_GET_PROTO_VER;
		msg.response_sz = 1;
		msg.sock_ind = 0;
		ret = hsmp_xfer(&msg, O_RDONLY);
		if (ret == ESMI_SUCCESS) {
			sm.hsmp_status = ESMI_INITIALIZED;
			sm.hsmp_proto_ver = msg.args[0];
			init_platform_info(&sm);
		}
	}

	if (sm.energy_status && sm.msr_status && sm.hsmp_status)
		sm.init_status = ESMI_NO_DRV;
	else
		sm.init_status = ESMI_INITIALIZED;
	psm = &sm;

	return sm.init_status;
}

void esmi_exit(void)
{
	if (psm && psm->map) {
		free(psm->map);
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
			(psm->msr_status == ESMI_NOT_INITIALIZED)) {\
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
	esmi_status_t ret;

	CHECK_ENERGY_GET_INPUT(penergy);
	if (core_ind >= psm->total_cores) {
		return ESMI_INVALID_INPUT;
	}
	core_ind %= psm->total_cores/psm->threads_per_core;

	if (!psm->energy_status)
		/*
		 * The hwmon enumeration of energy%d_input entries starts
		 * from 1.
		 */
		ret = read_energy_drv(core_ind + 1, penergy);

	else
		ret = read_msr_drv(core_ind, penergy, ENERGY_CORE_MSR);

	return errno_to_esmi_status(ret);
}

/*
 * Function to get the enenrgy of the socket with provided socket index
 */
esmi_status_t esmi_socket_energy_get(uint32_t sock_ind, uint64_t *penergy)
{
	esmi_status_t status;
	esmi_status_t ret;
	int core_ind;

	CHECK_ENERGY_GET_INPUT(penergy);
	if (sock_ind >= psm->total_sockets) {
		return ESMI_INVALID_INPUT;
	}

	if (!psm->energy_status) {
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
		ret = read_msr_drv(core_ind, penergy, ENERGY_PKG_MSR);
	}

	return errno_to_esmi_status(ret);
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

	if (!psm->energy_status)
		ret = batch_read_energy_drv(penergy, cpus);
	else
		ret = batch_read_msr_drv(penergy, cpus);

	return errno_to_esmi_status(ret);
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

	/* TODO check boostlimit against a valid range */
	if (core_ind >= psm->total_cores) {
		return ESMI_INVALID_INPUT;
	}

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
	char hot[9];

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
esmi_status_t esmi_xgmi_width_set(uint8_t min, uint8_t max)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;
	uint16_t width;
	int drv_val;
	int i;

	CHECK_HSMP_INPUT();

	if (psm->total_sockets < 2)
		return ESMI_NOT_SUPPORTED;

	if ((min > max) || (min > 2) || (max > 2))
		return ESMI_INVALID_INPUT;

	width = (min << 8) | max;
	for (i = 0; i < psm->total_sockets; i++) {
		msg.msg_id = HSMP_SET_XGMI_LINK_WIDTH;
		msg.num_args = 1;
		msg.args[0] = width;
		msg.sock_ind = i;
		ret = hsmp_xfer(&msg, O_WRONLY);
		if (ret)
			return errno_to_esmi_status(ret);
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

	if (pstate > 3)
		return ESMI_INVALID_INPUT;

	msg.num_args = 1;
	msg.sock_ind = sock_ind;
	msg.args[0] = pstate;
	ret = hsmp_xfer(&msg, O_WRONLY);

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_fclk_mclk_get(uint32_t sock_ind,
				 uint32_t *fclk, uint32_t *mclk)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;
	uint64_t clk;

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
	if (nbio_id > 3)
		return ESMI_INVALID_INPUT;
	if ((min > max) || (min > 3) || (max > 3))
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
	uint32_t dpm_val;

	msg.msg_id	= HSMP_GET_NBIO_DPM_LEVEL;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(dpm);

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	if (nbio_id > 3)
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

esmi_status_t esmi_ddr_bw_get(struct ddr_bw_metrics *ddr_bw)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;
	uint32_t bw;

	msg.msg_id = HSMP_GET_DDR_BANDWIDTH;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(ddr_bw);

	msg.response_sz = 1;
	msg.sock_ind = 0;
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
	esmi_status_t ret;
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

	/* Only Aggregate Banwdith is valid Bandwidth type for IO links */
	if (link.bw_type != 1)
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

esmi_status_t esmi_current_xgmi_bw_get(struct link_id_bw_type link,
				       uint32_t *xgmi_bw)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;
	int encode_val = 0;

	msg.msg_id	= HSMP_GET_XGMI_BANDWITH;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_GET_INPUT(xgmi_bw);

	if(validate_link_name(link.link_name, &encode_val))
		return ESMI_INVALID_INPUT;

	if (validate_bw_type(link.bw_type))
		return ESMI_INVALID_INPUT;

	msg.response_sz = 1;
	msg.num_args	= 1;
	msg.args[0]	= link.bw_type | encode_val << 8;
	ret = hsmp_xfer(&msg, O_RDONLY);
	if (!ret)
		*xgmi_bw = msg.args[0];

	return errno_to_esmi_status(ret);
}

static esmi_status_t validate_max_min_values(uint8_t max_value, uint8_t min_value,
					     uint8_t max_limit)
{
	if (max_value > max_limit | max_value < min_value)
		return ESMI_INVALID_INPUT;

	return ESMI_SUCCESS;

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

	ret = validate_max_min_values(max_link_width, min_link_width, psm->gmi3_link_width_limit);
	if (ret)
		return ret;

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

	if (rate_ctrl > psm->pci_gen5_rate_ctl)
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

static esmi_status_t validate_pwr_efficiency_mode(uint8_t mode)
{
	switch (mode) {
		case 0:
		case 1:
		case 2:
			return ESMI_SUCCESS;
		default:
			return ESMI_INVALID_INPUT;
	}
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

	if (validate_pwr_efficiency_mode(mode))
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.sock_ind	= sock_ind;
	msg.args[0]	= mode;
	ret = hsmp_xfer(&msg, O_RDWR);

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_df_pstate_range_set(uint8_t sock_ind, uint8_t max_pstate,
				       uint8_t min_pstate)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	msg.msg_id	= HSMP_SET_PSTATE_MAX_MIN;
	if (check_sup(msg.msg_id))
		return ESMI_NO_HSMP_MSG_SUP;

	CHECK_HSMP_INPUT();

	if (sock_ind >= psm->total_sockets)
		return ESMI_INVALID_INPUT;

	if (max_pstate > min_pstate || min_pstate > psm->df_pstate_max_limit)
		return ESMI_INVALID_INPUT;

	msg.num_args	= 1;
	msg.sock_ind	= sock_ind;
	msg.args[0]	= (min_pstate << 8) | max_pstate;
	ret = hsmp_xfer(&msg, O_WRONLY);

	return errno_to_esmi_status(ret);
}

esmi_status_t esmi_hsmp_proto_ver_get(uint32_t *proto_ver)
{
	struct hsmp_message msg = { 0 };
	esmi_status_t ret;

	if (psm->hsmp_proto_ver) {
		*proto_ver = psm->hsmp_proto_ver;
		return ESMI_SUCCESS;
	}

	msg.msg_id = HSMP_GET_PROTO_VER;

	CHECK_HSMP_GET_INPUT(proto_ver);

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
