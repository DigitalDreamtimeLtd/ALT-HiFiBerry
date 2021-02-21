// SPDX-License-Identifier: GPL-2.0
/*
 * dd-utils.c
 *
 * Author: Clive Messer <clive.messer@digitaldreamtime.co.uk>
 *         Copyright (c) Digital Dreamtime Ltd 2020-2021
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include "dd-utils.h"

//#include <linux/module.h>

#ifdef DDEBUG
char* dd_utils_log_daifmt_format(unsigned int format)
{
	switch(format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAI_FORMAT_I2S:
		return "I2S";
	case SND_SOC_DAI_FORMAT_RIGHT_J:
		return "RIGHT_J/LSB";
	case SND_SOC_DAI_FORMAT_LEFT_J:
		return "LEFT_J/MSB";
	case SND_SOC_DAI_FORMAT_DSP_A:
		return "DSP_A";
	case SND_SOC_DAI_FORMAT_DSP_B:
		return "DSP_B";
	case SND_SOC_DAI_FORMAT_AC97:
		return "AC97";
	case SND_SOC_DAI_FORMAT_PDM:
		return "PDM";
	default:
		return "UNKNOWN";
	}
}
//EXPORT_SYMBOL_GPL(dd_utils_log_daifmt_format);

char* dd_utils_log_daifmt_clock(unsigned int format)
{
	switch(format & SND_SOC_DAIFMT_CLOCK_MASK) {
	case SND_SOC_CLOCK_IN:
		return "CLOCK_IN";
	case SND_SOC_CLOCK_OUT:
		return "CLOCK_OUT";
	default:
		return "UNKNOWN";
	}
}
//EXPORT_SYMBOL_GPL(dd_utils_log_daifmt_clock);

char* dd_utils_log_daifmt_inverse(unsigned int format)
{
	switch(format & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		return "NB_NF (normal bclk / normal frame)";
	case SND_SOC_DAIFMT_NB_IF:
		return "NB_IF (normal bclk / invert frame)";
	case SND_SOC_DAIFMT_IB_NF:
		return "IB_NF (invert bclk / invert frame)";
	case SND_SOC_DAIFMT_IB_IF:
		return "IB_IF (invert bclk / invert frame)";
	default:
		return "UNKNOWN";
	}
}
//EXPORT_SYMBOL_GPL(dd_utils_log_daifmt_inverse);

char* dd_utils_log_daifmt_master(unsigned int format)
{
	switch(format & SND_SOC_DAIFMT_MASTER_MASK) {
        case SND_SOC_DAIFMT_CBM_CFM:
        	return "CBM_CFM (bclk master / frame master)";
        case SND_SOC_DAIFMT_CBS_CFM:
                return "CBS_CFM (bclk slave / frame master)";
        case SND_SOC_DAIFMT_CBM_CFS:
                return "CBM_CFS (bclk master / frame slave)";
        case SND_SOC_DAIFMT_CBS_CFS:
                return "CBS_CFS (bclk slave / frame slave)";
	default:
		return "UNKNOWN";
	}
}
//EXPORT_SYMBOL_GPL(dd_utils_log_daifmt_master);
#endif /* DDEBUG */

//MODULE_DESCRIPTION("Digital Dreamtine ASoC Utils");
//MODULE_AUTHOR("Clive Messer <clive.messer@digitaldreamtime.co.uk>");
//MODULE_LICENSE("GPL");
