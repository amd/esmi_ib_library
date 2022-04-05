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
#ifndef INCLUDE_E_SMI_E_SMI_H_
#define INCLUDE_E_SMI_E_SMI_H_

#include <stdbool.h>

#define ENERGY_DEV_NAME	"amd_energy"	//!< Supported Energy driver name

#define HSMP_CHAR_DEVFILE_NAME	"/dev/hsmp" //!< HSMP device path

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0])) //!< macro to calculate size

#define BIT(N)	(1 << N)		//!< macro for mask

/** \file e_smi.h
 *  Main header file for the E-SMI library.
 *  All required function, structure, enum, etc. definitions should be defined
 *  in this file.
 *
 *  @details  This header file contains the following:
 *  APIs prototype of the APIs exported by the E-SMI library.
 *  Description of the API, arguments and return values.
 *  The Error codes returned by the API.
 */

/**
 * @brief Deconstruct raw uint32_t into SMU firmware major and minor version numbers
 */
struct smu_fw_version {
        uint8_t debug;		//!< SMU fw Debug version number
        uint8_t minor;		//!< SMU fw Minor version number
        uint8_t major;		//!< SMU fw Major version number
        uint8_t unused;		//!< reserved fields
};

/**
 * @brief DDR bandwidth metrics.
 */
struct ddr_bw_metrics {
        uint32_t max_bw;	//!< DDR Maximum theoritical bandwidth in GB/s
        uint32_t utilized_bw;	//!< DDR bandwidth utilization in GB/s
        uint32_t utilized_pct;	//!< DDR bandwidth utilization in % of theoritical max
};

/**
 * @brief temperature range and refresh rate metrics of a DIMM
 */
struct temp_range_refresh_rate {
	uint8_t range : 3;	//!< temp range[2:0](3 bit data)
	uint8_t ref_rate : 1;	//!< DDR refresh rate mode[3](1 bit data)
};

/**
 * @brief DIMM Power(mW), power update rate(ms) and dimm address
 */
struct dimm_power {
	uint16_t power : 15;            //!< Dimm power consumption[31:17](15 bits data)
	uint16_t update_rate : 9;       //!< Time since last update[16:8](9 bit data)
	uint8_t dimm_addr;              //!< Dimm address[7:0](8 bit data)
};

/**
 * @brief DIMM temperature(°C) and update rate(ms) and dimm address
 */
struct dimm_thermal {
	uint16_t sensor : 11;           //!< Dimm thermal sensor[31:21](11 bit data)
	uint16_t update_rate : 9;       //!< Time since last update[16:8](9 bit data)
	uint8_t dimm_addr;              //!< Dimm address[7:0](8 bit data)
	float temp;			//!< temperature in degree celcius
};

/**
 * @brief xGMI Bandwidth Encoding types
 */
typedef enum {
	AGG_BW = BIT(0),	//!< Aggregate Bandwidth
	RD_BW = BIT(1),		//!< Read Bandwidth
	WR_BW = BIT(2)		//!< Write Bandwdith
} io_bw_encoding;

/**
 * @brief IO LINK and xGMI link Encoding values
 */
typedef enum {
	P0 = BIT(0),
	P1 = BIT(1),
	P2 = BIT(2),
	P3 = BIT(3),
	G0 = BIT(4),
	G1 = BIT(5),
	G2 = BIT(6),
	G3 = BIT(7)
} link_id_encoding;


/**
 * @brief LINK ID and Bandwidth type Information.It contains
 * LINK ID Encoding. Valid Link ID encodings are 1(P0), 2(P1),
 * 4(P2), 8(P3), 16(G0), 32(G1), 64(G2), 128(G3). Valid xGMI Bandwidth
 * types 1(Aggregate_BW), 2 (Read BW), 4 (Write BW).
 */
struct link_id_bw_type {
	io_bw_encoding bw_type;    //!< Bandwidth Type Information [1, 2, 4]
	link_id_encoding link_id;  //!< Link ID [1,2,4,8,16,32,64,128]
};

/**
 * @brief max and min LCLK DPM level on a given NBIO ID.
 * Valid max and min DPM level values are 0 - 1.
 */
struct dpm_level {
	uint8_t max_dpm_level;          //!< Max LCLK DPM level[15:8](8 bit data)
	uint8_t min_dpm_level;          //!< Min LCLK DPM level[7:0](8 bit data)
};

/**
 * @brief frequency limit source names
 */
static char * const freqlimitsrcnames[] = {
	"cHTC-Active",
	"PROCHOT",
	"TDC limit",
	"PPT Limit",
	"OPN Max",
	"Reliability Limit",
	"APML Agent",
	"HSMP Agent"
};

/**
 * @brief Error codes retured by E-SMI functions
 */
typedef enum {
	ESMI_SUCCESS = 0,	//!< Operation was successful
	ESMI_INITIALIZED = 0,	//!< ESMI initialized successfully
	ESMI_NO_ENERGY_DRV,	//!< Energy driver not found.
	ESMI_NO_MSR_DRV,	//!< MSR driver not found.
	ESMI_NO_HSMP_DRV,	//!< HSMP driver not found.
	ESMI_NO_HSMP_SUP,	//!< HSMP not supported.
	ESMI_NO_DRV,		//!< No Energy and HSMP driver present.
	ESMI_FILE_NOT_FOUND,	//!< file or directory not found
	ESMI_DEV_BUSY,          //!< Device or resource busy
	ESMI_PERMISSION,	//!< Permission denied/EACCESS file error.
				//!< Many functions require root access to run.
	ESMI_NOT_SUPPORTED,	//!< The requested information or
				//!< action is not available for the
				//!< given input, on the given system
	ESMI_FILE_ERROR,	//!< Problem accessing a file. This
				//!< may because the operation is not
				//!< supported by the Linux kernel
				//!< version running on the executing
				//!< machine
	ESMI_INTERRUPTED,	//!< An interrupt occurred during
				//!< execution of function
	ESMI_IO_ERROR,		//!< An input or output error
	ESMI_UNEXPECTED_SIZE,	//!< An unexpected amount of data
				//!< was read
	ESMI_UNKNOWN_ERROR,	//!< An unknown error occurred
	ESMI_ARG_PTR_NULL,	//!< Parsed argument is invalid
	ESMI_NO_MEMORY,		//!< Not enough memory to allocate
	ESMI_NOT_INITIALIZED,	//!< ESMI path not initialized
	ESMI_INVALID_INPUT,	//!< Input value is invalid
	ESMI_HSMP_TIMEOUT,	//!< HSMP message is timedout
	ESMI_NO_HSMP_MSG_SUP,	//!< HSMP message/feature not supported.
} esmi_status_t;

/**
 * @brief HSMP protocol version names
 */
enum hsmp_proto_versions {
	HSMP_PROTO_VER2 = 2,
	HSMP_PROTO_VER3,
	HSMP_PROTO_VER4,
	HSMP_PROTO_VER5
};

/****************************************************************************/
/** @defgroup InitShut Initialization and Shutdown
 *  This function validates the dependencies exists and initializes the library.
 *  @{
*/

/**
 *  @brief Initialize the library, validate the dependencies exists
 *
 *  @details Search the available dependency entries and initialize
 *  the library accordingly.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_init(void);

/**
 *  @brief Clean up allocation during init.
 */
void esmi_exit(void);

/** @} */  // end of InitShut

/****************************************************************************/
/** @defgroup EnergyQuer Energy Monitor (RAPL MSR)
 *  Below functions provide interfaces to get the core energy value for a
 *  given core and to get the socket energy value for a given socket.
 *  @{
*/

/**
 *  @brief Get the core energy for a given core.
 *
 *  @details Given a core index @p core_ind, and a @p penergy argument for
 *  64bit energy counter of that particular cpu, this function will read the
 *  energy counter of the given core and update the @p penergy in micro Joules.
 *
 *  Note: The energy status registers are accessed at core level. In a system
 *  with SMT enabled in BIOS, the sibling threads would report duplicate values.
 *  Aggregating the energy counters of the sibling threads is incorrect.
 *
 *  @param[in] core_ind is a core index
 *
 *  @param[inout] penergy Input buffer to return the core energy.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_core_energy_get(uint32_t core_ind, uint64_t *penergy);

/**
 *  @brief Get the socket energy for a given socket.
 *
 *  @details Given a socket index @p socket_idx, and a @p penergy argument
 *  for 64bit energy counter of a particular socket.
 *
 *  Updates the @p penergy with socket energy in micro Joules.
 *
 *  @param[in] socket_idx a socket index
 *
 *  @param[inout] penergy Input buffer to return the socket energy.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_energy_get(uint32_t socket_idx, uint64_t *penergy);

/**
 *  @brief Get energies of all cores in the system.
 *
 *  @details Given an argument for energy profile @p penergy, This function
 *  will read all core energies in an array @p penergy in micro Joules.
 *
 *  @param[inout] penergy Input buffer to return the energies of all cores.
 *  penergy should be allocated by user as below
 *  (esmi_number_of_cpus_get()/esmi_threads_per_core_get()) * sizeof (uint64_t)
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_all_energies_get(uint64_t *penergy);

/** @} */  // end of EnergyQuer

/****************************************************************************/
/** @defgroup SystemStatisticsQuer HSMP System Statistics
 *  Below functions to get HSMP System Statistics.
 *  @{
*/

/**
 *  @brief Get the SMU Firmware Version
 *
 *  @details This function will return the SMU FW version at @p smu_fw
 *
 *  @param[inout] smu_fw Input buffer to return the smu firmware version.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_smu_fw_version_get(struct smu_fw_version *smu_fw);

/**
 *  @brief Get normalized status of the processor's PROCHOT status.
 *  1 - PROCHOT active, 0 - PROCHOT inactive
 *
 *  @details Given a socket index @p socket_idx and this function will get
 *  PROCHOT at @p prochot.
 *
 *  @param[in] socket_idx a socket index
 *
 *  @param[inout] prochot Input buffer to return the PROCHOT status.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_prochot_status_get(uint32_t socket_idx, uint32_t *prochot);

/**
 *  @brief Get the Data Fabric clock and Memory clock in MHz, for a given
 *  socket index.
 *
 *  @details Given a socket index @p socket_idx and a pointer to a uint32_t
 *  @p fclk and @p mclk, this function will get the data fabric clock and
 *  memory clock.
 *
 *  @param[in] socket_idx a socket index
 *
 *  @param[inout] fclk Input buffer to return the data fabric clock.
 *
 *  @param[inout] mclk Input buffer to return the memory clock.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_fclk_mclk_get(uint32_t socket_idx,
				 uint32_t *fclk, uint32_t *mclk);

/**
 *  @brief Get the core clock (MHz) allowed by the most restrictive
 *  infrastructure limit at the time of the message.
 *
 *  @details Given a socket index @p socket_idx and a pointer to a uint32_t
 *  @p cclk, this function will get the core clock throttle limit.
 *
 *  @param[in] socket_idx a socket index
 *
 *  @param[inout] cclk Input buffer to return the core clock throttle limit.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */

esmi_status_t esmi_cclk_limit_get(uint32_t socket_idx, uint32_t *cclk);

/**
 *  @brief Get the HSMP interface (protocol) version.
 *
 *  @details This function will get the HSMP interface version at @p proto_ver
 *
 *  @param[inout] proto_ver Input buffer to return the hsmp protocol version.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_hsmp_proto_ver_get(uint32_t *proto_ver);

/**
 *  @brief Get the current active frequency limit of the socket.
 *
 *  @details This function will get the socket frequency and source of this limit
 *
 *  @param[in] sock_ind A socket index.
 *
 *  @param[inout] freq Input buffer to return the frequency(MHz).
 *
 *  @param[inout] src_type Input buffer to return the source of this limit
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_current_active_freq_limit_get(uint32_t sock_ind,
							uint16_t *freq, char **src_type);

/**
 *  @brief Get the Socket frequency range.
 *
 *  @details This function returns the socket frequency range, fmax
 *  and fmin.
 *
 *  @param[in] sock_ind Socket index.
 *
 *  @param[inout] fmax Input buffer to return the maximum frequency(MHz).
 *
 *  @param[inout] fmin Input buffer to return the minimum frequency(MHz).
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_freq_range_get(uint8_t sock_ind, uint16_t *fmax, uint16_t *fmin);

/**
 *  @brief Get the current active frequency limit of the core.
 *
 *  @details This function returns the core frequency limit for the specified core.
 *
 *  @param[in] core_id Core index.
 *
 *  @param[inout] freq Input buffer to return the core frequency limit(MHz)
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_current_freq_limit_core_get(uint32_t core_id, uint32_t *freq);

/** @} */  // end of SystemStatisticsQuer

/*****************************************************************************/
/** @defgroup PowerQuer Power Monitor
 *  Below functions provide interfaces to get the current power usage and
 *  Power Limits for a given socket.
 *  @{
*/

/**
 *  @brief Get the instantaneous power consumption of the provided socket.
 *
 *  @details Given a socket index @p socket_idx and a pointer to a uint32_t
 *  @p ppower, this function will get the current power consumption
 *  (in milliwatts) to the uint32_t pointed to by @p ppower.
 *
 *  @param[in] socket_idx a socket index
 *
 *  @param[inout] ppower Input buffer to return power consumption
 *  in the socket.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_power_get(uint32_t socket_idx, uint32_t *ppower);

/**
 *  @brief Get the current power cap value for a given socket.
 *
 *  @details This function will return the valid power cap @p pcap for a given
 *  socket @p socket_idx, this value will be used by the system to limit
 *  the power usage.
 *
 *  @param[in] socket_idx a socket index
 *
 *  @param[inout] pcap Input buffer to return power limit on the socket,
 *  in milliwatts.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_power_cap_get(uint32_t socket_idx, uint32_t *pcap);

/**
 *  @brief Get the maximum power cap value for a given socket.
 *
 *  @details This function will return the maximum possible valid power cap
 *  @p pmax from a @p socket_idx.
 *
 *  @param[in] socket_idx a socket index
 *
 *  @param[inout] pmax Input buffer to return maximum power limit on socket,
 *  in milliwatts.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_power_cap_max_get(uint32_t socket_idx,
					    uint32_t *pmax);

/**
 *  @brief Get the SVI based power telemetry for all rails.
 *
 *  @details This function returns the SVI based power telemetry for all rails.
 *
 *  @param[in] sock_ind Socket index.
 *
 *  @param[inout] power Input buffer to return the power(mW).
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_pwr_svi_telemetry_all_rails_get(uint32_t sock_ind, uint32_t *power);


/** @} */  // end of PowerQuer

/*****************************************************************************/

/** @defgroup PowerCont Power Control
 *  This function provides a way to control Power Limit.
 *  @{
 */

/**
 *  @brief Set the power cap value for a given socket.
 *
 *  @details This function will set the power cap to the provided value @p pcap.
 *  This cannot be more than the value returned by esmi_socket_power_cap_max_get().
 *
 *  Note: The power limit specified will be clipped to the maximum cTDP range for
 *  the processor. There is a limit on the minimum power that the processor can
 *  operate at, no further power socket reduction occurs if the limit is set
 *  below that minimum.
 *
 *  @param[in] socket_idx a socket index
 *
 *  @param[in] pcap a uint32_t that indicates the desired power cap, in
 *  milliwatts
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_power_cap_set(uint32_t socket_idx, uint32_t pcap);

/**
 *  @brief Set the power efficiency profile policy
 *
 *  @details This function will set the power efficiency mode.
 *
 *  Power efficiency modes are:
 *
 *  0 = High performance mode: This mode favours core performance.
 *  In this mode all df pstates are available and
 *  default df pstate and DLWM algorithms are active.
 *
 *  1 = Power efficient mode: This mode limits the boost frequency available to
 *  the cores and restricts the DF P-States. This mode also monitors the system load to 
 *  dynamically adjust performance for maximum power efficiency.
 *
 *  2 = IO performance mode: This mode sets up data fabric to maximize IO performance.
 *  This can result in lower core performance to increase the IO throughput.
 *
 *  @param[in] sock_ind A socket index.
 *
 *  @param[in] mode Power efficiency mode to be set.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_pwr_efficiency_mode_set(uint8_t sock_ind, uint8_t mode);

/** @} */  // end of PowerCont

/*****************************************************************************/

/** @defgroup PerfQuer Performance (Boost limit) Monitor
 *  This function provides the current boostlimit value for a given core.
 *  @{
 */

/**
 *  @brief Get the boostlimit value for a given core
 *
 *  @details This function will return the core's current boost limit
 *  @p pboostlimit for a particular @p cpu_ind
 *
 *  @param[in] cpu_ind a cpu index
 *
 *  @param[inout] pboostlimit Input buffer to return the boostlimit.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_core_boostlimit_get(uint32_t cpu_ind,
				       uint32_t *pboostlimit);

/**
 *  @brief Get the c0_residency value for a given socket
 *
 *  @details This function will return the socket's current c0_residency
 *  @p pc0_residency for a particular @p socket_idx
 *
 *  @param[in] socket_idx a socket index provided.
 *
 *  @param[inout] pc0_residency Input buffer to return the c0_residency.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_c0_residency_get(uint32_t socket_idx,
					   uint32_t *pc0_residency);

/** @} */  // end of PerfQuer

/*****************************************************************************/

/** @defgroup PerfCont Performance (Boost limit) Control
 *  Below functions provide ways to control Boost limit values.
 *  @{
 */

/**
 *  @brief Set the boostlimit value for a given core
 *
 *  @details This function will set the boostlimit to the provided value @p
 *  boostlimit for a given cpu @p cpu_ind.
 *
 *  @param[in] cpu_ind a cpu index is a given core to set the boostlimit
 *
 *  @param[in] boostlimit a uint32_t that indicates the desired boostlimit
 *  value of a given core
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_core_boostlimit_set(uint32_t cpu_ind, uint32_t boostlimit);

/**
 *  @brief Set the boostlimit value for a given socket.
 *
 *  @details This function will set the boostlimit to the provided value @p
 *  boostlimit for a given socket @p socket_idx.
 *
 *  @param[in] socket_idx a socket index to set boostlimit.
 *
 *  @param[in] boostlimit a uint32_t that indicates the desired boostlimit
 *  value of a particular socket.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_boostlimit_set(uint32_t socket_idx,
					 uint32_t boostlimit);

/** @} */  // end of PerfCont

/*****************************************************************************/

/** @defgroup ddrQuer ddr_bandwidth Monitor
 *  This function provides the DDR Bandwidth for a system
 *  @{
 */

/**
 *  @brief Get the Theoretical maximum DDR Bandwidth in GB/s,
 *  Current utilized DDR Bandwidth in GB/s and Current utilized
 *  DDR Bandwidth as a percentage of theoretical maximum in a system.
 *
 *  @details This function will return the DDR Bandwidth metrics @p ddr_bw
 *
 *  @param[inout] ddr_bw Input buffer to return the DDR bandwidth metrics,
 *  contains max_bw, utilized_bw and utilized_pct.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_ddr_bw_get(struct ddr_bw_metrics *ddr_bw);

/** @} */  // end of ddrQuer

/*****************************************************************************/
/** @defgroup TempQuer Temperature Query
 *  This function provides the current tempearature value in degree C.
 *  @{
 */

/**
 *  @brief Get temperature monitor for a given socket
 *
 *  @details This function will return the socket's current temperature
 *  in milli degree celsius @p ptmon for a particular @p sock_ind.
 *
 *  @param[in] sock_ind a socket index provided.
 *
 *  @param[inout] ptmon pointer to a uint32_t that indicates the
 *  possible tmon value.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_temperature_get(uint32_t sock_ind, uint32_t *ptmon);

/** @} */  // end of TempQuer

/*****************************************************************************/
/** @defgroup DimmStatisticsQuer Dimm statistics
 *  This function provides the dimm temperature, power and update rates.
 *  @{
 */

/**
 *  @brief Get dimm temperature range and refresh rate
 *
 *  @details This function returns the per DIMM temperature range and
 *  refresh rate from the MR4 register.
 *
 *  @param[in] sock_ind Socket index through which the DIMM can be accessed
 *
 *  @param[in] dimm_addr DIMM identifier, follow "HSMP DIMM Addres encoding".
 *
 *  @param[inout] rate Input buffer of type struct temp_range_refresh_rate with refresh
 *  rate and temp range.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_dimm_temp_range_and_refresh_rate_get(uint8_t sock_ind, uint8_t dimm_addr,
							struct temp_range_refresh_rate *rate);

/**
 *  @brief Get dimm power consumption and update rate
 *
 *  @details This function returns the DIMM power and update rate
 *
 *  @param[in] sock_ind Socket index through which the DIMM can be accessed.
 *
 *  @param[in] dimm_addr DIMM identifier, follow "HSMP DIMM Addres encoding".
 *
 *  @param[inout] dimm_pow Input buffer of type struct dimm_power containing power(mW),
 *  update rate(ms) and  dimm address.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_dimm_power_consumption_get(uint8_t sock_ind, uint8_t dimm_addr,
					      struct dimm_power *dimm_pow);

/**
 *  @brief Get dimm thermal sensor
 *
 *  @details This function will return the DIMM thermal sensor(2 sensors per DIMM)
 *  and update rate
 *
 *  @param[in] sock_ind Socket index through which the DIMM can be accessed.
 *
 *  @param[in] dimm_addr DIMM identifier, follow "HSMP DIMM Addres encoding".
 *
 *  @param[inout] dimm_temp Input buffer of type struct dimm_thermal which contains
 *  temperature(°C), update rate(ms) and dimm address
 *  Update rate value can vary from 0 to 511ms.
 *  Update rate of "0" means last update was < 1ms and 511ms means update was >= 511ms.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_dimm_thermal_sensor_get(uint8_t sock_ind, uint8_t dimm_addr,
					   struct dimm_thermal *dimm_temp);
/** @} */  // end of DimmStatisticsQuer

/*****************************************************************************/
/** @defgroup xGMIBwCont xGMI bandwidth control
 *  This function provides a way to control xgmi bandwidth connected in 2P systems.
 *  @{
 */

/**
 *  @brief Set xgmi width for a multi socket system
 *
 *  @details This function will set the xgmi width @p min and @p max for all
 *  the sockets in the system
 *
 *  @param[in] min minimum xgmi link width, varies from 0 to 2 with min <= max.
 *
 *  @param[in] max maximum xgmi link width, varies from 0 to 2.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_xgmi_width_set(uint8_t min, uint8_t max);

/** @} */  // end of xGMIBwCont

/*****************************************************************************/
/** @defgroup GMI3WidthCont GMI3 width control
 *  This function provides a way to control global memory interconnect bandwidth.
 *  @{
 */

/**
 *  @brief Set gmi3 width
 *
 *  @details This function will set the global memory interconnect width.
 *  Values can be 0, 1 or 2.
 *  0 = Quarter width
 *  1 = Half width
 *  2 = Full width
 *
 *  @param[in] sock_ind Socket index.
 *
 *  @param[in] min_link_width Minimum link width to be set.
 *
 *  @param[in] max_link_width Maximum link width to be set.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_gmi3_link_width_range_set(uint8_t sock_ind, uint8_t min_link_width,
					     uint8_t max_link_width);

/** @} */  // end of GMI3WidthCont

/*****************************************************************************/
/** @defgroup PStateCont APB and LCLK level control
 *  This functions provides a way to control APB and lclk values.
 *  @{
 */

/**
 *  @brief Enable automatic P-state selection
 *
 *  @details Given a socket index @p sock_ind, this function will enable
 *  performance boost algorithm
 *  By default, an algorithm adjusts DF P-States automatically in order to
 *  optimize performance. However, this default may be changed to a fixed
 *  DF P-State through a CBS option at boottime.
 *  APBDisable may also be used to disable this algorithm and force a fixed
 *  DF P-State.
 *
 *  NOTE: While the socket is in PC6 or if PROCHOT_L is asserted, the lowest
 *  DF P-State (highest value) is enforced regardless of the APBEnable/APBDisable
 *  state.
 *
 *  @param[in] sock_ind a socket index
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_apb_enable(uint32_t sock_ind);

/**
 *  @brief Set data fabric P-state to user specified value
 *
 *  @details This function will set the desired P-state at @p pstate.
 *  Acceptable values for the P-state are 0(highest) - 3 (lowest).
 *
 *  @param[in] sock_ind a socket index
 *
 *  @param[in] pstate a uint8_t that indicates the desired P-state to set.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_apb_disable(uint32_t sock_ind, uint8_t pstate);

/**
 *  @brief Set lclk dpm level
 *
 *  @details This function will set the lclk dpm level / nbio pstate
 *  for the specified @p nbio_id in a specified socket @p sock_ind with provided
 *  values @p min and @p max.
 *
 *  @param[in] sock_ind socket index.
 *
 *  @param[in] nbio_id northbridge number varies from 0 to 3.
 *
 *  @param[in] min pstate minimum value, varies from 0 to 3 with min <= max
 *
 *  @param[in] max pstate maximum value, varies from 0 to 3.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_lclk_dpm_level_set(uint32_t sock_ind, uint8_t nbio_id,
					     uint8_t min, uint8_t max);

/**
 *  @brief Get lclk dpm level
 *
 *  @details This function will get the lclk dpm level.
 *  DPM lelvel is an encoding to represent PCIe link frequency
 *
 *  @param[in] sock_ind Socket index
 *
 *  @param[in] nbio_id  NBIO id(0-3)
 *
 *  @param[inout] nbio Input buffer of struct dpm_level type to hold min and max dpm levels
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_lclk_dpm_level_get(uint8_t sock_ind, uint8_t nbio_id,
					     struct dpm_level *nbio);

/**
 *  @brief Set pcie link rate
 *
 *  @details This function will set the pcie link rate to gen4/5 or
 *  auto detection based on bandwidth utilisation.
 *  Value are:
 *  0 = auto detect bandwidth utilisation and set link rate
 *  1 = Limit at gen4 rate
 *  2 = Limit at gen5 rate
 *
 *  @param[in] sock_ind Socket index.
 *
 *  @param[in] rate_ctrl Control value to be set.
 *
 *  @param[inout] prev_mode Input buffer to hold the previous mode.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_pcie_link_rate_set(uint8_t sock_ind, uint8_t rate_ctrl, uint8_t *prev_mode);

/**
 *  @brief Set data fabric pstate range.
 *
 *  @details This function will set the max and min pstates for the data fabric.
 *  Acceptable values for the P-state are 0(highest) - 4 (lowest) with
 *  max <= min.
 *
 *  @param[in] sock_ind a socket index.
 *
 *  @param[in] max_pstate Maximum pstate value to be set.
 *
 *  @param[in] min_pstate Minimum pstate value to be set.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_df_pstate_range_set(uint8_t sock_ind, uint8_t max_pstate, uint8_t min_pstate);

/** @} */  // end of PStateCont

/*****************************************************************************/
/** @defgroup BwQuer Bandwidth Query
 *  This function provides the IO and xGMI bandiwtdh.
 *  @{
 */

/**
 *  @brief Get IO bandwidth on IO link.
 *
 *  @details This function returns the IO Aggregate bandwidth for the given link id.
 *
 *  @param[in] sock_ind Socket index.
 *
 *  @param[in] link  structure containing link_id(Link encoding values of given link) and bwtype
 *  info.
 *
 *  @param[inout] io_bw Input buffer for bandwidth data in Mbps.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_current_io_bandwidth_get(uint8_t sock_ind, struct link_id_bw_type link,
					    uint32_t *io_bw);

/**
 *  @brief Get xGMI bandwidth.
 *
 *  @details This function will get the xGMI Aggregate bandwidth for the specified link
 *  in a multi socket system.
 *
 *  @param[in] link  structure containing link_id(Link encoding values of given link) and bwtype
 *  info.
 *
 *  @param[inout] xgmi_bw Input buffer for bandwidth data in Mbps.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_current_xgmi_bw_get(struct link_id_bw_type link,
				       uint32_t *xgmi_bw);

/** @} */  // end of BwQuer

/*****************************************************************************/
/** @defgroup AuxilQuer Auxiliary functions
 *  Below functions provide interfaces to get the total number of cores and
 *  sockets available and also to get the first online core on a given socket
 *  in the system.
 *  @{
*/

/**
 *  @brief Get the CPU family
 *
 *  @param[inout] family Input buffer to return the cpu family.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 */
esmi_status_t esmi_cpu_family_get(uint32_t *family);

/**
 *  @brief Get the CPU model
 *
 *  @param[inout] model Input buffer to reurn the cpu model.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 */
esmi_status_t esmi_cpu_model_get(uint32_t *model);

/**
 *  @brief Get the number of threads per core in the system
 *
 *  @param[inout] threads input buffer to return number of SMT threads.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 */
esmi_status_t esmi_threads_per_core_get(uint32_t *threads);

/**
 *  @brief Get the number of cpus available in the system
 *
 *  @param[inout] cpus input buffer to return number of cpus,
 *  reported by nproc (including threads in case of SMT enable).
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 */
esmi_status_t esmi_number_of_cpus_get(uint32_t *cpus);

/**
 *  @brief Get the total number of sockets available in the system
 *
 *  @param[inout] sockets input buffer to return number of sockets.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 */
esmi_status_t esmi_number_of_sockets_get(uint32_t *sockets);

/**
 *  @brief Get the first online core on a given socket.
 *
 *  @param[in] socket_idx a socket index provided.
 *
 *  @param[inout] pcore_ind input buffer to return the index of first online
 *  core in the socket.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 */
esmi_status_t esmi_first_online_core_on_socket(uint32_t socket_idx,
					       uint32_t *pcore_ind);

/**
 * @brief Get the error string message for esmi errors.
 *
 *  @details Get the error message for the esmi error numbers
 *
 *  @param[in] esmi_err is a esmi error number
 *
 *  @retval char* value returned upon successful call.
 */
char * esmi_get_err_msg(esmi_status_t esmi_err);

/** @} */  // end of AuxilQuer

/*****************************************************************************/

#endif  // INCLUDE_E_SMI_E_SMI_H_
