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

/**
 * MONITOR TYPES
 * @brief This enum gives information to identify different energy monitor types
 */
typedef enum {
	ENERGY_TYPE,				//!< Core and Socket Energy coordinate
	MSR_SAFE_TYPE,				//!< RAPL MSR Energy read coordinate
	MONITOR_TYPE_MAX			//!< Max Monitor Type coordinate
} monitor_types_t;

int read_energy_drv(uint32_t sensor_id, uint64_t *val);
int read_msr_drv(uint32_t sensor_id, uint64_t *pval, uint64_t reg);
int batch_read_energy_drv(uint64_t *pval, uint32_t cpus);
int batch_read_msr_drv(uint64_t *pval, uint32_t cpus);

int find_energy(char *devname, char *hwmon_name);
int find_msr(const char *path);
int hsmp_xfer(struct hsmp_message *msg, int mode);

#endif  // INCLUDE_E_SMI_E_SMI_MONITOR_H_
