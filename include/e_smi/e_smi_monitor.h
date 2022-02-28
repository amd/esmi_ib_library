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
 * @brief Path used to get the total number of CPUs in the system.
 */
#define CPU_COUNT_PATH "/sys/devices/system/cpu/present"

/**
 * @brief Sysfs directory path for hwmon devices.
 */
#define HWMON_PATH "/sys/class/hwmon"

/**
 * @brief The core sysfs directory.
 */
#define CPU_SYS_PATH "/sys/devices/system/cpu"

/**
 * MONITOR TYPES
 * @brief This enum gives information to identify whether the monitor type is
 * from Energy/HWMON or HSMP.
 */
typedef enum {
	ENERGY_TYPE,				//!< Core and Socket Energy coordinate
	HSMP_TEST_TYPE,
	SMU_FW_VERSION_TYPE,			//!< SMU firmware version coordinate
	HSMP_PROTO_VER_TYPE,			//!< HSMP interface version coordinate
	SOCKET_POWER_TYPE,			//!< Socket Power coordinate
	W_SOCKET_POWER_LIMIT_TYPE,		//!< Write socket Power Limit coordinate
	R_SOCKET_POWER_LIMIT_TYPE,		//!< Read socket Power Limit coordinate
	SOCKET_POWER_LIMIT_MAX_TYPE,		//!< Socket PowerLimit Max coordinate
	W_CORE_BOOSTLIMIT_TYPE,			//!< Write core Boostlimit coordinate
	SOCKET_BOOSTLIMIT_TYPE,			//!< Socket Boostlimit coordinate
	R_CORE_BOOSTLIMIT_TYPE,			//!< Read core Boostlimit coordinate
	PROCHOT_STATUS_TYPE,			//!< HSMP prochot status coordinate
	XGMI_WIDTH_TYPE,			//!< HSMP xgmi width coordinate
	DIS_DF_PSTATE_TYPE,			//!< HSMP disable DF P-state coordinate
	EN_DF_PSTATE_TYPE,			//!< HSMP enable DF P-state coordinate
	FCLK_MEMCLK_TYPE,			//!< Current fclk, memclk coordinate
	CCLK_LIMIT_TYPE,			//!< Core clock limit coordinate
	SOCKET_C0_RESIDENCY_TYPE,		//!< Socket c0 residency coordinate
	LCLKDPM_LEVEL,				//!< Socket nbio pstate coordinate
	PKG_BOOSTLIMIT_TYPE,			//!< Package Boostlimit coordinate
	DDR_BW_TYPE,				//!< DDR bandwidth coordinate
	SOCKET_TEMP_MONITOR_TYPE,		//!< Socket temperature monitor coordinate
	DIMM_TEMP_RANGE_REFRESH_RATE_TYPE,	//!< Dimm temp range and refresh rate
	DIMM_POWER_CONSUMPTION_TYPE,		//!< Dimm temp range and refresh rate
	DIMM_THERMAL_SENSOR_TYPE,		//!< Dimm thermal sensor
	CURRENT_ACTIVE_FREQ_LIMIT_SOCKET_TYPE,	//!< Socket frequency limit
	CURRENT_ACTIVE_FREQ_LIMIT_CORE_TYPE,	//!< Cclk limit set per core
	PWR_SVI_TELEMTRY_SOCKET_TYPE,		//!< SVI based power telemetry
	SOCKET_FREQ_RANGE_TYPE,			//!< Socket frequeny range fmax and fmin
	CURRENT_IO_BANDWIDTH_TYPE,		//!< Bandwidth on IO link
	CURRENT_XGMI_BANDWIDTH_TYPE,		//!< Bandwidth on XGMI link
	MONITOR_TYPE_MAX			//!< Max Monitor Type coordinate
} monitor_types_t;

int read_energy(monitor_types_t type, uint32_t sensor_id, uint64_t *val);
int batch_read_energy(monitor_types_t type, uint64_t *pval, uint32_t entries);

int hsmp_read64(monitor_types_t type, uint32_t sensor_id, uint64_t *pval);
int hsmp_read32(monitor_types_t type, uint32_t sensor_id, uint32_t *val);
int hsmp_readstr(monitor_types_t type, uint32_t sensor_id, char *val, uint32_t len);
int hsmp_write_s32(monitor_types_t type, uint32_t sensor_id, int32_t val);
int hsmp_write32(monitor_types_t type, uint32_t sensor_id, uint32_t val);

int find_energy(char *devname, char *hwmon_name);
int find_hsmp(const char *path);
int hsmp_xfer(struct hsmp_message *msg, int mode);

#endif  // INCLUDE_E_SMI_E_SMI_MONITOR_H_
