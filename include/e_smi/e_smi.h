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
#define HSMP_DEV_NAME	"amd_hsmp"	//!< Supported HSMP driver name

#define HSMP_CHAR_DEVFILE_NAME	"/dev/hsmp" //!< HSMP device path

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
 * @brief Error codes retured by E-SMI functions
 */
typedef enum {
	ESMI_SUCCESS = 0,	//!< Operation was successful
	ESMI_INITIALIZED = 0,	//!< ESMI initialized successfully
	ESMI_NO_ENERGY_DRV,	//!< Energy driver not found.
	ESMI_NO_HSMP_DRV,	//!< HSMP driver not found.
	ESMI_NO_HSMP_SUP,	//!< HSMP feature not supported.
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
	ESMI_INVALID_INPUT	//!< Input value is invalid
} esmi_status_t;

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

/**
 *  @brief Set the boostlimit value for the package (whole system).
 *
 *  @details This function will set the boostlimit to the provided value @p
 *  boostlimit for the whole package.
 *
 *  @param[in] boostlimit a uint32_t that indicates the desired boostlimit
 *  value of the package.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_package_boostlimit_set(uint32_t boostlimit);

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

/** @} */  // end of PStateCont
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
