/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright (c) 2020-2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 */
#include <stdio.h>
#include <stdint.h>
#include <e_smi/e_smi.h>
#include <e_smi/e_smi_monitor.h>

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

/* Turin - hsmp_proto_ver7 */
/* MSGID1h - MSGID32h */
static bool tbl_turin[] = { false, true, true, true, true, true, true, true, true, true,
			    /* MSGID-0xA */
			    true, true, true, true, true, true, true, true, true, true,
			    /* MSGID-0x14 */
			    true, false, true, true, true, true, true, true, true, true,
			    /* MSGID-0x1e */
			    true, true, true, true, true, false, false, false, true,
			    /* MSGID-0x27-0x28 */
			    true, true, false, false, false, false, false, false, false,
			    /* MSGID-0x30-0x32 */
			    true, true, true };

/* F1A_M50_5F - hsmp_proto_ver7 */
/* MSGID1h - MSGID32h */
static bool tbl_F1A_M50_5F[] = {
			    false, true, true, true, true, true, true, true, true, true,
			    /*0x00,0x01, 0x02, 0x03, 0x04, 0x05, 0x06  0x07  0x08  0x09*/
			    true, true, true, true, true, true, true, true, true, true,
			    /*0xA,0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13*/
			    true, false, true, true, true, true, true, true, true, true,
			    /*0x14,0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D*/
			    true, false, false, true, true, false, false, false, true,
			    /*0x1E,0x1F,0x20, 0x21, 0x22,  0x23,  0x24,  0x25, 0x26*/
			    true, true, true, true, false, false, false, false, false,
			    /*0x27,0x28,0x29, 0x2A,  0x2B,  0x2C,  0x2D,  0x2E,  0x2F*/
			    true, true, true, true, true, true, true };
			    /* 0x30, 0x31, 0x32, 0x33, 0x34, 0x35 0x36 */

bool *lut = NULL;
int lut_size = 0;

/* encoding values are as per link_names array order in src/e_smi.c */
/* xgmi and io link encodings on genoa(proto ver5) platforms */
static struct link_encoding proto_ver5_encoding[] = { {"P0", BIT(0)}, {"P1", BIT(1)}, {"P2", BIT(2)},
						      {"P3", BIT(3)}, {"G0", BIT(4)}, {"G1", BIT(5)},
						      {"G2", BIT(6)}, {"G3", BIT(7)}, {"P4", BIT(8)},
						      {"P5", BIT(9)}, {NULL, -1} };

/* xgmi and io link encodings on mi300(proto ver6) platforms */
static struct link_encoding proto_ver6_encoding[] = { {"P2", 0x3}, {"P3", 0x4}, {"G0", 0x8}, {"G1", 0x9},
						      {"G2", 0xA}, {"G3", 0xB}, {"G4", 0xC}, {"G5", 0xD},
						      {"G6", 0xE}, {"G7", 0xF}, {NULL, -1} };

#define PCI_GEN5_RATE_CTRL		0x2
#define DF_PSTATE_MAX_LIMIT		0x2
#define GMI3_LINK_WIDTH_LIMIT		0x2
#define MAX_PWR_EFF_MODE_FAM0x19	0x3
#define MAX_PWR_EFF_MODE_FAM0x1A	0x5
#define MAX_DPM_LEVEL			0x3
#define MAX_DPM_LEVEL_MI300		0x2

/* Assign platform specific values from the documentation */
void init_platform_info(struct system_metrics *sm)
{
	switch (sm->hsmp_proto_ver)
	{
		case HSMP_PROTO_VER2:
			lut = tbl_milan;
			lut_size = ARRAY_SIZE(tbl_milan);
			sm->lencode = NULL;
			sm->max_dpm_level = MAX_DPM_LEVEL;
			break;
		case HSMP_PROTO_VER4:
			lut = tbl_trento;
			lut_size = ARRAY_SIZE(tbl_trento);
			sm->lencode = NULL;
			sm->max_dpm_level = MAX_DPM_LEVEL;
			break;
		case HSMP_PROTO_VER5:
			sm->df_pstate_max_limit = DF_PSTATE_MAX_LIMIT;
			sm->gmi3_link_width_limit = GMI3_LINK_WIDTH_LIMIT;
			sm->pci_gen5_rate_ctl = PCI_GEN5_RATE_CTRL;
			sm->lencode = proto_ver5_encoding;
			sm->max_dpm_level = MAX_DPM_LEVEL;
			if (sm->cpu_family == 0x1A && sm->cpu_model <= 0x1F) {
				lut = tbl_turin;
				lut_size = ARRAY_SIZE(tbl_turin);
				sm->max_pwr_eff_mode = MAX_PWR_EFF_MODE_FAM0x1A;
				sm->hsmp_rapl_reading = true;
			} else {
				lut = tbl_genoa;
				lut_size = ARRAY_SIZE(tbl_genoa);
				sm->max_pwr_eff_mode = MAX_PWR_EFF_MODE_FAM0x19;
			}
			break;
		case HSMP_PROTO_VER6:
			lut = tbl_mi300;
			lut_size = ARRAY_SIZE(tbl_mi300);
			sm->lencode = proto_ver6_encoding;
			sm->max_dpm_level = MAX_DPM_LEVEL_MI300;
			break;
		case HSMP_PROTO_VER7:
		default:
			sm->df_pstate_max_limit = DF_PSTATE_MAX_LIMIT;
			sm->gmi3_link_width_limit = GMI3_LINK_WIDTH_LIMIT;
			sm->pci_gen5_rate_ctl = PCI_GEN5_RATE_CTRL;
			sm->lencode = proto_ver5_encoding;
			sm->max_pwr_eff_mode = MAX_PWR_EFF_MODE_FAM0x1A;
			sm->hsmp_rapl_reading = true;
			sm->max_dpm_level = MAX_DPM_LEVEL;
			if((0x1A == sm->cpu_family) && ((sm->cpu_model >= 0x50) && (sm->cpu_model <= 0x5F)))
			{
				lut = tbl_F1A_M50_5F;
				lut_size = ARRAY_SIZE(tbl_F1A_M50_5F);
			}
			else
			{
				lut = tbl_turin;
				lut_size = ARRAY_SIZE(tbl_turin);
			}
			break;
	}
}
