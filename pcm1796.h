/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * definitions for PCM1796
 *
 * Copyright 2013 Amarula Solutions
 *
 * Author: Clive Messer <clive.messer@digitaldreamtime.co.uk>
 *         Copyright (c) Digital Dreamtime Ltd 2020-2021
 */

#ifndef __PCM1796_H__
#define __PCM1796_H__

#define PCM1796_REG16	16
#define PCM1796_REG17	17
#define PCM1796_REG18	18
#define PCM1796_REG19	19
#define PCM1796_REG20	20
#define PCM1796_REG21	21
#define PCM1796_REG22	22
#define PCM1796_REG23	23

/*
 * Register 16: ATL: Digital Attenuation Level Setting
 *		bit 7:0, RW, Default=0xFF (11111111)
 *
 * Each DAC output has a digital attenuator associated with it. The attenuator
 * can be set from 0 dB to –120 dB, in 0.5-dB steps. Alternatively, the
 * attenuator can be set to infinite attenuation (or mute).
 * The attenuation data for each channel can be set individually. However, the
 * data load control (the ATLD bit of control register 18) is common to both
 * attenuators. ATLD must be set to 1 in order to change an attenuator setting.
 *
 * The attenuation level can be set using the following formula:
 *  Attenuation level (dB) = 0.5 dB • (ATx[7:0] DEC – 255)
 * where ATx[7:0] DEC = 0 through 255
 *
 * For ATx[7:0] DEC = 0 through 14, the attenuator is set to infinite
 * attenuation.
 */
#define PCM1796_REG16_ATL		PCM1796_REG16
#define PCM1796_REG16_ATL_SHIFT		0
#define PCM1796_REG16_ATL_MASK		(255 << PCM1796_REG16_ATL_SHIFT)

/*
 * Register 17: ATR: Digital Attenuation Level Setting
 *		bit 7:0, RW, Default=0xFF (11111111)
 */
#define PCM1796_REG17_ATR		PCM1796_REG17
#define PCM1796_REG17_ATR_SHIFT		0
#define PCM1796_REG17_ATR_MASK		(255 << PCM1796_REG17_ATL_SHIFT)

/*
 * Register 18: ATLD: Attenuation Load Control
 *		bit 7, RW, Default=0x00
 *
 * The ATLD bit is used to enable loading of the attenuation data contained in
 * registers 16 and 17. When ATLD = 0, the attenuation settings remain at the
 * previously programmed levels, ignoring new data loaded from registers 16
 * and 17. When ATLD = 1, attenuation data written to registers 16 and 17 is
 * loaded normally.
 */
#define PCM1796_REG18_ATLD		PCM1796_REG18
#define PCM1796_REG18_ATLD_SHIFT	7
#define PCM1796_REG18_ATLD_MASK		(1 << PCM1796_REG18_ATLD_SHIFT)
#define PCM1796_REG18_ATLD_DISABLE	(0 << PCM1796_REG18_ATLD_SHIFT)
#define PCM1796_REG18_ATLD_ENABLE	(1 << PCM1796_REG18_ATLD_SHIFT)

/*
 * Register 18: FMT: Audio Interface Data Format
 *		bit 6:4, RW, Default=0x05
 *
 * The FMT bits are used to select the data format for the serial audio
 * interface.
 *
 * For the external digital filter interface mode (DFTH mode), this register
 * is operated as shown in the APPLICATION FOR EXTERNAL DIGITAL FILTER
 * INTERFACE section of this data sheet.
 */
#define PCM1796_REG18_FMT		PCM1796_REG18
#define PCM1796_REG18_FMT_SHIFT		4
#define PCM1796_REG18_FMT_MASK		(7 << PCM1796_REG18_FMT_SHIFT)
#define PCM1796_REG18_FMT_RJ16		(0 << PCM1796_REG18_FMT_SHIFT)
#define PCM1796_REG18_FMT_RJ20		(1 << PCM1796_REG18_FMT_SHIFT)
#define PCM1796_REG18_FMT_RJ24		(2 << PCM1796_REG18_FMT_SHIFT)
#define PCM1796_REG18_FMT_LJ24		(3 << PCM1796_REG18_FMT_SHIFT)
#define PCM1796_REG18_FMT_I2S16		(4 << PCM1796_REG18_FMT_SHIFT)
#define PCM1796_REG18_FMT_I2S24		(5 << PCM1796_REG18_FMT_SHIFT)

/*
 * Register 18: DMF: Sampling Frequency Selection for the De-Emphasis Function
 *		bit 3:2, RW, Default=0x00
 *
 * The DMF bits are used to select the sampling frequency used by the digital
 * de-emphasis function when it is enabled by setting the DME bit.
 * The de-emphasis curves are shown in the TYPICAL PERFORMANCE CURVES section
 * of this data sheet.
 *
 * For the DSD mode, analog FIR filter performance can be selected using this
 * register. A register map and filter response plots are shown in the
 * APPLICATION FOR DSD FORMAT (DSD MODE) INTERFACE section of this data sheet.
 */
#define PCM1796_REG18_DMF		PCM1796_REG18
#define PCM1796_REG18_DMF_SHIFT		2
#define PCM1796_REG18_DMF_MASK		(3 << PCM1796_REG18_DMF_SHIFT)
#define PCM1796_REG18_DMF_DISABLED	(0 << PCM1796_REG18_DMF_SHIFT)
#define PCM1796_REG18_DMF_48K		(1 << PCM1796_REG18_DMF_SHIFT)
#define PCM1796_REG18_DMF_44K		(2 << PCM1796_REG18_DMF_SHIFT)
#define PCM1796_REG18_DMF_32K		(3 << PCM1796_REG18_DMF_SHIFT)

/*
 * Register 18: DME: Digital De-Emphasis Control
 *		bit 1, RW, Default=0x00
 *
 * The DME bit is used to enable or disable the de-emphasis function for both
 * channels.
 */
#define PCM1796_REG18_DME		PCM1796_REG18
#define PCM1796_REG18_DME_SHIFT		1
#define PCM1796_REG18_DME_MASK		(1 << PCM1796_REG18_DME_SHIFT)
#define PCM1796_REG18_DME_DISABLE	(0 << PCM1796_REG18_DME_SHIFT)
#define PCM1796_REG18_DME_ENABLE	(1 << PCM1796_REG18_DME_SHIFT)

/*
 * Register 18: MUTE: Soft Mute Control
 *		bit 0, RW, Default=0x00
 *
 * The MUTE bit is used to enable or disable the soft mute function for both
 * channels.
 *
 * Soft mute is operated as a 256-step attenuator. The speed for each step to
 * –infinity dB (mute) is determined by the attenuation rate selected in the
 * ATS register.
 */
#define PCM1796_REG18_MUTE		PCM1796_REG18
#define PCM1796_REG18_MUTE_SHIFT	0
#define PCM1796_REG18_MUTE_MASK		(1 << PCM1796_REG18_MUTE_SHIFT)
#define PCM1796_REG18_MUTE_DISABLE	(0 << PCM1796_REG18_MUTE_SHIFT)
#define PCM1796_REG18_MUTE_ENABLE	(1 << PCM1796_REG18_MUTE_SHIFT)

/*
 * Register 19: REV: Output Phase Reversal
 *		bit 7, RW, Default=0x00
 *
 * The REV bit is used to invert the output phase for both channels.
 */
#define PCM1796_REG19_REV		PCM1796_REG19
#define PCM1796_REG19_REV_SHIFT		7
#define PCM1796_REG19_REV_MASK		(1 << PCM1796_REG19_REV_SHIFT)
#define PCM1796_REG19_REV_NORMAL	(0 << PCM1796_REG19_REV_SHIFT)
#define PCM1796_REG19_REV_INVERT	(1 << PCM1796_REG19_REV_SHIFT)

/*
 * Register 19: ATS: Attenuation Rate Select
 *		bit 6:5, RW, Default=0x00
 *
 * The ATS bits are used to select the rate at which the attenuator is
 * decremented/incremented during level transitions.
 */
#define PCM1796_REG19_ATS		PCM1796_REG19
#define PCM1796_REG19_ATS_SHIFT		5
#define PCM1796_REG19_ATS_MASK		(3 << PCM1796_REG19_ATS_SHIFT)
#define PCM1796_REG19_ATS_LRCK		(0 << PCM1796_REG19_ATS_SHIFT)
#define PCM1796_REG19_ATS_LRCK_DIV2	(1 << PCM1796_REG19_ATS_SHIFT)
#define PCM1796_REG19_ATS_LRCK_DIV4	(2 << PCM1796_REG19_ATS_SHIFT)
#define PCM1796_REG19_ATS_LRCK_DIV8	(3 << PCM1796_REG19_ATS_SHIFT)

/*
 * Register 19: OPE: DAC Operation Control
 *		bit 4, RW, Default=0x00
 *
 * The OPE bit is used to enable or disable the analog output for both channels.
 * Disabling the analog outputs forces them to the bipolar zero level (BPZ)
 * even if audio data is present on the input.
 */
#define PCM1796_REG19_OPE		PCM1796_REG19
#define PCM1796_REG19_OPE_SHIFT		4
#define PCM1796_REG19_OPE_MASK		(1 << PCM1796_REG19_OPE_SHIFT)
#define PCM1796_REG19_OPE_ENABLE	(0 << PCM1796_REG19_OPE_SHIFT)
#define PCM1796_REG19_OPE_DISABLE	(1 << PCM1796_REG19_OPE_SHIFT)

/*
 * Register 19: DFMS: Stereo DF Bypass Mode Select
 *		bit 2, RW, Default=0x00
 *
 * The DFMS bit is used to enable stereo operation in DF bypass mode. In the
 * DF bypass mode, when DFMS is set to 0, the pin for the input data is DATA
 * (pin 5) only, therefore the PCM1796 operates as a monaural DAC. When DFMS
 * is set to 1, the PCM1796 can operate as a stereo DAC with inputs of L-channel
 * and R-channel data on ZEROL (pin 1) and ZEROR (pin 2), respectively.
 */
#define PCM1796_REG19_DFMS		PCM1796_REG19
#define PCM1796_REG19_DFMS_SHIFT	2
#define PCM1796_REG19_DFMS_MASK		(1 << PCM1796_REG19_DFMS_SHIFT)
#define PCM1796_REG19_DFMS_MONO		(0 << PCM1796_REG19_DFMS_SHIFT)
#define PCM1796_REG19_DFMS_STEREO	(1 << PCM1796_REG19_DFMS_SHIFT)

/*
 * Register 19: FLT: Digital Filter Rolloff Control
 *		bit 1, RW, Default=0x00
 *
 * The FLT bit is used to select the digital filter rolloff characteristic.
 * The filter responses for these selections are shown in the TYPICAL
 * PERFORMANCE CURVES section of this data sheet.
 */
#define PCM1796_REG19_FLT		PCM1796_REG19
#define PCM1796_REG19_FLT_SHIFT		1
#define PCM1796_REG19_FLT_MASK		(1 << PCM1796_REG19_FLT_SHIFT)
#define PCM1796_REG19_FLT_SHARP		(0 << PCM1796_REG19_FLT_SHIFT)
#define PCM1796_REG19_FLT_SLOW		(1 << PCM1796_REG19_FLT_SHIFT)

/*
 * Register 19: INZD: Infinite Zero Detect Mute Control
 *		bit 0, RW, Default=0x00
 *
 * The INZD bit is used to enable or disable the zero detect mute function.
 * Setting INZD to 1 forces muted analog outputs to hold a bipolar zero level
 * when the PCM1796 detects a zero condition in both channels. The infinite
 * zero detect mute function is not available in the DSD mode.
 */
#define PCM1796_REG19_INZD		PCM1796_REG19
#define PCM1796_REG19_INZD_SHIFT 	0
#define PCM1796_REG19_INZD_MASK		(1 << PCM1796_REG19_INZD_SHIFT)
#define PCM1796_REG19_INZD_DISABLE	(0 << PCM1796_REG19_INZD_SHIFT)
#define PCM1796_REG19_INZD_ENABLE	(1 << PCM1796_REG19_INZD_SHIFT)

/*
 * Register 20: SRST: System Reset Control
 *		bit 6, WRITE ONLY, Default=0x00
 *
 * The SRST bit is used to reset the PCM1796 to the initial system condition.
 */
#define PCM1796_REG20_SRST		PCM1796_REG20
#define PCM1796_REG20_SRST_SHIFT	6
#define PCM1796_REG20_SRST_MASK		(1 << PCM1796_REG20_SRST_SHIFT)
#define PCM1796_REG20_SRST_NORMAL	(0 << PCM1796_REG20_SRST_SHIFT)
#define PCM1796_REG20_SRST_RESET	(1 << PCM1796_REG20_SRST_SHIFT)

/*
 * Register 20: DSD: DSD Interface Mode Control
 *		bit 5, RW, Default=0x00
 *
 * The DSD bit is used to enable or disable the DSD interface mode.
 */
#define PCM1796_REG20_DSD		PCM1796_REG20
#define PCM1796_REG20_DSD_SHIFT		5
#define PCM1796_REG20_DSD_MASK		(1 << PCM1796_REG20_DSD_SHIFT)
#define PCM1796_REG20_DSD_DISABLE	(0 << PCM1796_REG20_DSD_SHIFT)
#define PCM1796_REG20_DSD_ENABLE	(1 << PCM1796_REG20_DSD_SHIFT)

/*
 * Register 20: DFTH: Digital Filter Bypass (or Through Mode) Control
 *		bit 4, RW, Default= 0x00
 *
 * The DFTH bit is used to enable or disable the external digital filter
 * interface mode.
 */
#define PCM1796_REG20_DFTH		PCM1796_REG20
#define PCM1796_REG20_DFTH_SHIFT	4
#define PCM1796_REG20_DFTH_MASK		(1 << PCM1796_REG20_DFTH_SHIFT)
#define PCM1796_REG20_DFTH_ENABLE	(0 << PCM1796_REG20_DFTH_SHIFT)
#define PCM1796_REG20_DFTH_DISABLE	(1 << PCM1796_REG20_DFTH_SHIFT)

/*
 * Register 20: MONO: Monaural Mode Selection
 *		bit 3, RW, Default=0x00
 *
 * The MONO function is used to change operation mode from the normal stereo
 * mode to the monaural mode. When the monaural mode is selected, both DACs
 * operate in a balanced mode for one channel of audio input data. Channel
 * selection is available for L-channel or R-channel data, determined by the
 * CHSL bit as described immediately following.
 */
#define PCM1796_REG20_MONO		PCM1796_REG20
#define PCM1796_REG20_MONO_SHIFT	3
#define PCM1796_REG20_MONO_MASK		(1 << PCM1796_REG20_MONO_SHIFT)
#define PCM1796_REG20_MONO_STEREO	(0 << PCM1796_REG20_MONO_SHIFT)
#define PCM1796_REG20_MONO_MONO		(1 << PCM1796_REG20_MONO_SHIFT)

/*
 * Register 20: CHSL: Channel Selection for Monaural Mode
 *		bit 2, RW, Default=0x00
 *
 * The CHSL bit selects L-channel or R-channel data to be used in monaural mode.
 */
#define PCM1796_REG20_CHSL		PCM1796_REG20
#define PCM1796_REG20_CHSL_SHIFT	2
#define PCM1796_REG20_CHSL_MASK		(1 << PCM1796_REG20_CHSL_SHIFT)
#define PCM1796_REG20_CHSL_LEFT		(0 << PCM1796_REG20_CHSL_SHIFT)
#define PCM1796_REG20_CHSL_RIGHT	(1 << PCM1796_REG20_CHSL_SHIFT)

/*
 * Register 20: OS: Delta-Sigma Oversampling Rate Selection
 *		bit 1:0, RW, Default=0x00
 *
 * The OS bits are used to change the oversampling rate of delta-sigma
 * modulation. Use of this function enables the designer to stabilize the
 * conditions at the post low-pass filter for different sampling rates. As an
 * application example, programming to set 128 times in 44.1kHz operation, 64
 * times in 96kHz operation, and 32 times in 192kHz operation allows the use
 * of only a single type (cutoff frequency) of post low-pass filter. The 128fS
 * oversampling rate is not available at sampling rates above 100 kHz. If the
 * 128fS oversampling rate is selected, a system clock of more than 256fS is
 * required.
 *
 * In DSD mode, these bits are used to select the speed of the bit clock for
 * DSD data coming into the analog FIR filter.
 */
#define PCM1796_REG20_OS		PCM1796_REG20
#define PCM1796_REG20_OS_SHIFT		0
#define PCM1796_REG20_OS_MASK		(3 << PCM1796_REG20_OS_SHIFT)
#define PCM1796_REG20_OS_64		(0 << PCM1796_REG20_OS_SHIFT)
#define PCM1796_REG20_OS_32		(1 << PCM1796_REG20_OS_SHIFT)
#define PCM1796_REG20_OS_128		(2 << PCM1796_REG20_OS_SHIFT)

/*
 * Register 21: DZ: DSD Zero Output Enable
 *		bit 2:1, RW, Default=0x00
 *
 * The DZ bits are used to enable or disable the output zero flags, and to
 * select the zero pattern in the DSD mode.
 */
#define PCM1796_REG21_DZ		PCM1796_REG21
#define PCM1796_REG21_DZ_SHIFT		1
#define PCM1796_REG21_DZ_MASK		(3 << PCM1796_REG21_DZ_SHIFT)
#define PCM1796_REG21_DZ_DISABLE	(0 << PCM1796_REG21_DZ_SHIFT)
#define PCM1796_REG21_DZ_EVEN		(1 << PCM1796_REG21_DZ_SHIFT)
#define PCM1796_REG21_DZ_96H		(2 << PCM1796_REG21_DZ_SHIFT)

/*
 * Register 21: PCMZ: PCM Zero Output Enable
 *		bit 0, RW, Default=0x01
 *
 * The PCMZ bit is used to enable or disable the output zero flags in the PCM
 * mode and the external DF mode.
 */
#define PCM1796_REG21_PCMZ		PCM1796_REG21
#define PCM1796_REG21_PCMZ_SHIFT	0
#define PCM1796_REG21_PCMZ_MASK		(1 << PCM1796_REG21_PCMZ_SHIFT)
#define PCM1796_REG21_PCMZ_DISABLE	(0 << PCM1796_REG21_PCMZ_SHIFT)
#define PCM1796_REG21_PCMZ_ENABLE	(1 << PCM1796_REG21_PCMZ_SHIFT)

/*
 * Register 22: ZFGR: Zero-Detection Flag
 *		bit 1, READ ONLY, Default=0x00
 *
 * These bits show zero conditions. Their status is the same as that of the
 * zero flags at ZEROL (pin 1) and ZEROR (pin 2). See Zero Detect in the
 * FUNCTION DESCRIPTIONS section.
 */
#define PCM1796_REG22_ZFGR		PCM1796_REG22
#define PCM1796_REG22_ZFGR_SHIFT	1
#define PCM1796_REG22_ZFGR_MASK		(1 << PCM1796_REG22_ZFGR_SHIFT)
#define PCM1796_REG22_ZFGR_NOT_ZERO	(0 << PCM1796_REG22_ZFGR_SHIFT)
#define PCM1796_REG22_ZFGR_ZERO		(1 << PCM1796_REG22_ZFGR_SHIFT)

/*
 * Register 22: ZFGL: Zero-Detection Flag
 *		bit 1, READ ONLY, Default=0x00
 */
#define PCM1796_REG22_ZFGL		PCM1796_REG22
#define PCM1796_REG22_ZFGL_SHIFT	0
#define PCM1796_REG22_ZFGL_MASK		(1 << PCM1796_REG22_ZFGL_SHIFT)
#define PCM1796_REG22_ZFGL_NOT_ZERO	(0 << PCM1796_REG22_ZFGL_SHIFT)
#define PCM1796_REG22_ZFGL_ZERO		(1 << PCM1796_REG22_ZFGL_SHIFT)

/*
 * Register 23: ID: Device ID
 *		bit 4:0, READ ONLY
 *
 * The ID bits hold a device ID in the TDMCA mode.
 */
#define PCM1796_REG23_ID		PCM1796_REG23
#define PCM1796_REG23_ID_SHIFT		0
#define PCM1796_REG23_ID_MASK		(31 << PCM1796_REG23_ID_SHIFT)

/* MAX sysclk in I2C fast mode */
#define PCM1796_MAX_SYSCLK		36864000

#define PCM1796_SYSCLK_ID		0x00

#define PCM1796_FORMATS (SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S24_LE \
				| SNDRV_PCM_FMTBIT_S16_LE)

extern const struct regmap_config pcm1796_regmap_cfg;

int pcm1796_probe(struct device *dev, struct regmap *regmap);

void pcm1796_remove(struct device *dev);
#endif
