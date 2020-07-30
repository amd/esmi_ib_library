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

#define FILEPATHSIZ	512 //!< Buffer to hold size of sysfs filepath
#define DRVPATHSIZ	256 //!< size of driver location path
#define FILESIZ		128 //!< size of filename

/**
 * @brief Path used to get the total number of CPUs in the system.
 */
#define CPU_COUNT_PATH "/sys/devices/system/cpu/present"

/**
 * @brief Path used to get the total number of sockets in the system.
 */
#define SOCKET_COUNT_PATH "/sys/devices/system/node/possible"

/**
 * @brief Sysfs directory path for hwmon devices.
 */
#define HWMON_PATH "/sys/class/hwmon"

/**
 * @brief The core sysfs directory.
 */
#define CPU_PATH "/sys/devices/system/cpu"

/**
 * MONITOR TYPES
 * @brief This enum gives information to identify whether the monitor type is
 * from Energy/HWMON or HSMP.
 */
typedef enum {
	ENERGY_TYPE,	        //!< Core and Socket Energy coordinate
	PKG_BOOSTLIMIT_TYPE,	//!< Package Boostlimit coordinate
	CORE_BOOSTLIMIT_TYPE,	//!< Core Boostlimit coordinate
	SOCKET_BOOSTLIMIT_TYPE,	//!< Socket Boostlimit coordinate
	SOCKET_POWER_TYPE,	//!< Socket Power coordinate
	SOCKET_POWER_LIMIT_TYPE,//!< Socket Power Limit coordinate
	SOCKET_POWER_LIMIT_MAX_TYPE,//!< Socket PowerLimit Max coordinate
	SOCKET_TCTL_TYPE,	//!< Socket tctl coordinate
	SOCKET_C0_RESIDENCY_TYPE,//!< Socket c0 residency coordinate
	MONITOR_TYPE_MAX	//!< Max Monitor Type coordinate
} monitor_types_t;

char energymon_path[DRVPATHSIZ], hsmpmon_path[DRVPATHSIZ];

int read_energy(monitor_types_t type, uint32_t sensor_id, uint64_t *val);

int hsmp_read32(monitor_types_t type, uint32_t sensor_id, uint32_t *val);
int hsmp_write32(monitor_types_t type, uint32_t sensor_id, uint32_t val);

int find_energy(char *devname, char *hwmon_name);
int find_hsmp(const char *path);

#endif  // INCLUDE_E_SMI_E_SMI_MONITOR_H_
