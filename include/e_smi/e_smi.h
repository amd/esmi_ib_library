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

#define ENERGY_DEV_NAME	"amd_energy"	//!< Supported Energy driver name
#define HSMP_DEV_NAME	"amd_hsmp"	//!< Supported HSMP driver name

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
 * @brief Error codes retured by E-SMI functions
 */
typedef enum {
	ESMI_SUCCESS = 0,	//!< Operation was successful
	ESMI_INITIALIZED = 0,	//!< ESMI initialized successfully
	ESMI_NO_ENERGY_DRV,	//!< Energy driver not found.
	ESMI_NO_HSMP_DRV,	//!< HSMP driver not found.
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
 *  energy profile of that particular cpu, this function will read the
 *  energy counter of the given core and update the @p penergy in micro Joules.
 *
 *  Note: The energy status registers are accessed at core level. In a system
 *  with SMT enabled in BIOS, the sibling threads would report duplicate values.
 *  Aggregating the energy counters of the sibling threads is incorrect.
 *
 *  @param[in] core_ind is a core index
 *
 *  @param[inout] penergy The energy profile of a core
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_core_energy_get(uint32_t core_ind, uint64_t *penergy);

/**
 *  @brief Get the socket energy for a given socket.
 *
 *  @details Given a scoket index @p socket_ind, and a @p penergy argument
 *  for energy profile of a particular socket. This function identifies an
 *  online cpu of the specific socket and reads the socket energy counter.
 *
 *  Updates the @p penergy with socket energy in micro Joules.
 *
 *  @param[in] socket_ind a socket index
 *
 *  @param[inout] penergy The energy profile of a socket
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_energy_get(uint32_t socket_ind, uint64_t *penergy);

/**
 *  @brief Get energies of all cores and sockets in the system.
 *
 *  @details Given an argument for energy profile @p penergy, and @p entries
 *  number of energy entries to read. This function will read all core and
 *  socket energies in an array @p penergy in micro Joules.
 *
 *  @param[out] penergy Address of allocated entries to get all energies.
 *
 *  @param[in] entries number of energy entries to read.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_all_energies_get(uint64_t *penergy, uint32_t entries);

/** @} */  // end of EnergyQuer

/*****************************************************************************/
/** @defgroup PowerQuer Power Monitor
 *  Below functions provide interfaces to get the current power usage and
 *  Power Limits for a given socket.
 *  @{
*/

/**
 *  @brief Get the average power consumption of the socket with provided
 *  socket index.
 *
 *  @details Given a socket index @p socket_ind and a pointer to a uint32_t
 *  @p ppower, this function will get the current average power consumption
 *  (in milliwatts) to the uint32_t pointed to by @p ppower.
 *
 *  @param[in] socket_ind a socket index
 *
 *  @param[inout] ppower a pointer to uint32_t to which the average power
 *  consumption will get
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_power_avg_get(uint32_t socket_ind, uint32_t *ppower);

/**
 *  @brief Get the current power cap value for a given socket.
 *
 *  @details This function will return the valid power cap @p pcap for a given
 *  socket @p socket_ind, this value will be used by the system to limit
 *  the power usage.
 *
 *  @param[in] socket_ind a socket index
 *
 *  @param[inout] pcap a pointer to a uint32_t that indicates the valid
 *  possible power cap, in milliwatts
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_power_cap_get(uint32_t socket_ind, uint32_t *pcap);

/**
 *  @brief Get the maximum power cap value for a given socket.
 *
 *  @details This function will return the maximum possible valid power cap
 *  @p pmax from a @p socket_ind.
 *
 *  @param[in] socket_ind a socket index
 *
 *  @param[inout] pmax a pointer to a uint32_t that indicates the maximum
 *  possible power cap, in milliwatts
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_power_cap_max_get(uint32_t socket_ind,
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

 *  @param[in] socket_ind a socket index
 *
 *  @param[in] pcap a uint32_t that indicates the desired power cap, in
 *  milliwatts
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_power_cap_set(uint32_t socket_ind, uint32_t pcap);

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
 *  @param[inout] pboostlimit pointer to a uint32_t that indicates the
 *  possible boost limit value
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_core_boostlimit_get(uint32_t cpu_ind,
				       uint32_t *pboostlimit);

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
 *  boostlimit for a given socket @p socket_ind.
 *
 *  @param[in] socket_ind a socket index to set boostlimit.
 *
 *  @param[in] boostlimit a uint32_t that indicates the desired boostlimit
 *  value of a particular socket.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_boostlimit_set(uint32_t socket_ind,
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

/** @defgroup TctlQuer Tctl Monitor
 *  This function provides the current tctl value for a given socket.
 *  @{
 */

/**
 *  @brief Get the tctl value for a given socket
 *
 *  @details This function will return the socket's current tctl
 *  @p ptctl for a particular @p sock_ind.
 *
 *  @param[in] sock_ind a socket index provided.
 *
 *  @param[inout] ptctl pointer to a uint32_t that indicates the
 *  possible tctl value.
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_tctl_get(uint32_t sock_ind, uint32_t *ptctl);

/** @} */  // end of TctlQuer

/*****************************************************************************/

/** @defgroup c0_ResidencyQuer c0_residency Monitor
 *  This function provides the current c0_residency value for a given socket.
 *  @{
 */

/**
 *  @brief Get the c0_residency value for a given socket
 *
 *  @details This function will return the socket's current c0_residency
 *  @p pc0_residency for a particular @p sock_ind
 *
 *  @param[in] sock_ind a socket index provided.
 *
 *  @param[inout] pc0_residency pointer to a uint32_t that indicates the
 *  possible c0_residency value
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 *
 */
esmi_status_t esmi_socket_c0_residency_get(uint32_t sock_ind,
					   uint32_t *pc0_residency);

/** @} */  // end of c0_ResidencyQuer

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
 *  @param[out] family cpu family number
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 */
esmi_status_t esmi_cpu_family_get(uint32_t *family);

/**
 *  @brief Get the CPU model
 *
 *  @param[out] model cpu model number
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 */
esmi_status_t esmi_cpu_model_get(uint32_t *model);

/**
 *  @brief Get the number of threads per core in the system
 *
 *  @param[out] threads number of threads per core in the system
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 */
esmi_status_t esmi_threads_per_core_get(uint32_t *threads);

/**
 *  @brief Get the number of cpus available in the system
 *
 *  @param[out] cpus number of cpus in the system
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 */
esmi_status_t esmi_number_of_cpus_get(uint32_t *cpus);

/**
 *  @brief Get the total number of sockets available in the system
 *
 *  @param[out] sockets number of sockets in the system
 *
 *  @retval ::ESMI_SUCCESS is returned upon successful call.
 *  @retval None-zero is returned upon failure.
 */
esmi_status_t esmi_number_of_sockets_get(uint32_t *sockets);

/**
 * @brief Get the first online core on a given socket.
 *
 *  @details Get the online core belongs to particular socket with provided
 *  socket index
 *
 *  @param[in] socket_id is a socket index
 *
 *  @retval int value returned upon successful call.
 */
int esmi_get_online_core_on_socket(int socket_id);

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
