/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright (c) 2022, Advanced Micro Devices, Inc.
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
#include <stdio.h>
#include <stdint.h>
#include <e_smi/e_smi.h>
#include <e_smi/e_smi_monitor.h>

#define MILAN_TBL_SIZE (sizeof tbl_milan / sizeof tbl_milan[0])
#define TRENTO_TBL_SIZE (sizeof tbl_trento / sizeof tbl_trento[0])
#define GENOA_TBL_SIZE (sizeof tbl_genoa / sizeof tbl_genoa[0])
#define MI300_TBL_SIZE (sizeof tbl_mi300 / sizeof tbl_mi300[0])

/* Genesis - hsmp_proto_ver2 */
/* MSGID1h - MSGID14h */
static bool tbl_milan[] = { false, true, true, true, true, true, true, true, true, true,
			    /* MSGID-0xA */
			    true, true, true, true, true, true, true, true, true, true,
			    /* MSGID-0x14 */
			    true };

/* Badami - hsmp_proto_ver4 */
/* MSGID1h - MSGID15h */
static bool tbl_trento[] = { false, true, true, true, true, true, true, true, true, true,
			     /* MSGID-0xA */
			     true, true, true, true, true, true, true, true, true, true,
			     /* MSGID-0x14 */
			     true, true };

/* Stones - hsmp_proto_ver5 */
/* MSGID1h - MSGID22h */
static bool tbl_genoa[] = { false, true, true, true, true, true, true, true, true, true,
			    /* MSGID-0xA */
			    true, true, true, true, true, true, true, true, true, true,
			    /* MSGID-0x14 */
			    true, false, true, true, true, true, true, true, true, true,
			    /* MSGID-0x1e */
			    true, true, true, true, true };

/* MI300A - hsmp_proto_ver6 */
/* MSGID1h - MSGID46h */
static bool tbl_mi300[] = { false, true, true, true, true, true, true, true, true, true,
			    /* MSGID-0xA */
			    true, true, true, false, false, true, true, true, true, true,
			    /* MSGID-0x14 */
			    false, false, false, false, false, true, true, true, true, true,
			    /* MSGID-0x1e */
			    true, false, false, false, false, true, true, true, false, false,
			    /* MSGID-0x28 */
			    false, false, false, false, false, false, false, false, true, true,
			    /* MSGID-0x32 */
			    false, false, false, false, false, false, false, false, false, false,
			    /* MSGID-0x3c */
			    false, false, false, false, false, false, false, false, false, true,
			    /* MSGID-0x46 */
			    true };

bool *lut = NULL;
int lut_size = 0;

/* Assign platform specific values from the documentation */
void init_platform_info(struct system_metrics *sm)
{
	switch (sm->hsmp_proto_ver)
	{
		case HSMP_PROTO_VER2:
			lut = tbl_milan;
			lut_size = MILAN_TBL_SIZE;
			break;
		case HSMP_PROTO_VER4:
			lut = tbl_trento;
			lut_size = TRENTO_TBL_SIZE;
			break;
		case HSMP_PROTO_VER5:
			sm->df_pstate_max_limit = 2;
			sm->gmi3_link_width_limit = 2;
			sm->pci_gen5_rate_ctl = 2;
			lut = tbl_genoa;
			lut_size = GENOA_TBL_SIZE;
			break;
		case HSMP_PROTO_VER6:
			lut = tbl_mi300;
			lut_size = MI300_TBL_SIZE;
			break;
		default:
			sm->df_pstate_max_limit = 3;
			sm->gmi3_link_width_limit = 0;
			sm->pci_gen5_rate_ctl = 0;
			break;
	}
}
