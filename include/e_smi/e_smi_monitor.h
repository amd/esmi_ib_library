/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright (c) 2020-2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 */
#ifndef INCLUDE_E_SMI_E_SMI_MONITOR_H_
#define INCLUDE_E_SMI_E_SMI_MONITOR_H_

/** \file e_smi_monitor.h
 *  Header file which is common for the  E-SMI library implementation.
 *
 *  @brief The E-SMI library api is new, This API function is used to create
 *  a particular entry parameter path from HSMP and Energy driver which are
 *  created by driver installation.
 */

#include <stdint.h>
#include <asm/amd_hsmp.h>
#include <e_smi/e_smi.h>

#define FILEPATHSIZ	512 //!< Buffer to hold size of sysfs filepath
#define DRVPATHSIZ	256 //!< size of driver location path
#define FILESIZ		128 //!< size of filename

/**
 * @brief RAPL MSR registers used for total energy consumed.
 */
#define ENERGY_PWR_UNIT_MSR     0xC0010299
#define ENERGY_CORE_MSR         0xC001029A
#define ENERGY_PKG_MSR          0xC001029B

#define AMD_ENERGY_UNIT_MASK    0x1F00
#define AMD_ENERGY_UNIT_OFFSET  8

/**
 * @brief Path used to get the total number of CPUs in the system.
 */
#define CPU_COUNT_PATH "/sys/devices/system/cpu/present"

/**
 * @brief Sysfs directory path for hwmon devices.
 */
#define HWMON_PATH "/sys/class/hwmon"

/**
 * @brief energy monitor through MSR Driver.
 */
#define MSR_PATH "/dev/cpu"

/**
 * @brief The core sysfs directory.
 */
#define CPU_SYS_PATH "/sys/devices/system/cpu"

struct link_encoding {
        char *name;
        int val;
};

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
	esmi_status_t msr_status;	// MSR driver status
	esmi_status_t msr_safe_status;	// MSR safe driver status
	esmi_status_t hsmp_status;	// hsmp driver status
	struct cpu_mapping *map;
	uint8_t df_pstate_max_limit;	// df pstate maximum limit
	uint8_t gmi3_link_width_limit;	// gmi3 maximum link width
	uint8_t pci_gen5_rate_ctl;
	struct link_encoding *lencode;	// holds platform specifc link encodings
	uint8_t max_pwr_eff_mode;	// maximum allowed power efficiency mode
};

/**
 * MONITOR TYPES
 * @brief This enum gives information to identify different energy monitor types
 */
typedef enum {
	ENERGY_TYPE,				//!< Core and Socket Energy coordinate
	MSR_SAFE_TYPE,				//!< RAPL MSR Energy read coordinate
	MSR_TYPE,				//!< RAPL MSR Energy read coordinate
	MONITOR_TYPE_MAX			//!< Max Monitor Type coordinate
} monitor_types_t;

int read_energy_drv(uint32_t sensor_id, uint64_t *val);
int read_msr_drv(monitor_types_t type, uint32_t sensor_id, uint64_t *pval, uint64_t reg);
int batch_read_energy_drv(uint64_t *pval, uint32_t cpus);
int batch_read_msr_drv(monitor_types_t type, uint64_t *pval, uint32_t cpus);

int find_energy(char *devname, char *hwmon_name);
int find_msr_safe();
int find_msr();
int hsmp_xfer(struct hsmp_message *msg, int mode);
void init_platform_info(struct system_metrics *sm);

#endif  // INCLUDE_E_SMI_E_SMI_MONITOR_H_
