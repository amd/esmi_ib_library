/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright (c) 2020-2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 */
#ifndef INCLUDE_E_SMI_E_SMI_UTILS_H_
#define INCLUDE_E_SMI_E_SMI_UTILS_H_

/** \file e_smi_utils.h
 *  Header file for the device specific file access.
 *
 *  @brief These functions holds the sysfs access helper functions for HSMP
 *  driver and Energy driver.
 *
 *  The sysfs path can be accessed by reading and writing using below APIs.
 */
int readsys_u32(char *filepath, uint32_t *pval);
int writesys_s32(char *filepath, int32_t val);
int writesys_u32(char *filepath, uint32_t val);
int readsys_u64(char *filepath, uint64_t *pval);
int readsys_str(char *filepath, char *pval, uint32_t val);
int readmsr_u64(char *filepath, uint64_t *pval, uint64_t reg);

#endif  // INCLUDE_E_SMI_E_SMI_UTILS_H_
