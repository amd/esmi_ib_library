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

bool *lut = NULL;
int lut_size = 0;

/* encoding values are as per link_names array order in src/e_smi.c */
/* xgmi and io link encodings on genoa(proto ver5) platforms */
static struct link_encoding proto_ver5_encoding[] = { {"P0", BIT(0)}, {"P1", BIT(1)}, {"P2", BIT(2)},
						      {"P3", BIT(3)}, {"G0", BIT(4)}, {"G1", BIT(5)},
						      {"G2", BIT(6)}, {"G3", BIT(7)}, {NULL, -1} };

/* xgmi and io link encodings on mi300(proto ver6) platforms */
static struct link_encoding proto_ver6_encoding[] = { {"P2", 0x3}, {"P3", 0x4}, {"G0", 0x8}, {"G1", 0x9},
						      {"G2", 0xA}, {"G3", 0xB}, {"G4", 0xC}, {"G5", 0xD},
						      {"G6", 0xE}, {"G7", 0xF}, {NULL, -1} };

/* Assign platform specific values from the documentation */
void init_platform_info(struct system_metrics *sm)
{
	switch (sm->hsmp_proto_ver)
	{
		case HSMP_PROTO_VER2:
			lut = tbl_milan;
			lut_size = ARRAY_SIZE(tbl_milan);
			sm->lencode = NULL;
			break;
		case HSMP_PROTO_VER4:
			lut = tbl_trento;
			lut_size = ARRAY_SIZE(tbl_trento);
			sm->lencode = NULL;
			break;
		case HSMP_PROTO_VER5:
			sm->df_pstate_max_limit = 2;
			sm->gmi3_link_width_limit = 2;
			sm->pci_gen5_rate_ctl = 2;
			sm->lencode = proto_ver5_encoding;
			lut = tbl_genoa;
			lut_size = ARRAY_SIZE(tbl_genoa);
			break;
		case HSMP_PROTO_VER6:
			lut = tbl_mi300;
			lut_size = ARRAY_SIZE(tbl_mi300);
			sm->lencode = proto_ver6_encoding;
			break;
		default:
			lut = tbl_mi300;
			lut_size = ARRAY_SIZE(tbl_mi300);
			sm->lencode = proto_ver6_encoding;
			break;
	}
}
