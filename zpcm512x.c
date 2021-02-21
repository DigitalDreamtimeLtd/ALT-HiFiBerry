// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the PCM512x CODECs
 *
 * Author:	Mark Brown <broonie@kernel.org>
 *		Copyright 2014 Linaro Ltd
 *
 * Author: Clive Messer <clive.messer@digitaldreamtime.co.uk>
 *         Copyright (c) Digital Dreamtime Ltd 2016-2021
 */

#include <linux/gpio/consumer.h>
#include <linux/of_gpio.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/gcd.h>
#include <linux/version.h>
#include <sound/soc.h>
//#include <sound/soc-dapm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>

#include "zpcm512x.h"
#include "dd-utils.h"

#ifdef PCM512X_GPIO_ACTIVE_HIGH
#define PCM512X_GPIOD_OUT_LOW	GPIOD_OUT_LOW
#else /* Pi gpio default is active_low, so need to set logical high */
#define PCM512X_GPIOD_OUT_LOW	GPIOD_OUT_HIGH
#endif /* PCM512X_GPIO_ACTIVE_HIGH */

#define PCM512x_NUM_SUPPLIES 3
static const char * const zpcm512x_supply_names[PCM512x_NUM_SUPPLIES] = {
	"AVDD",
	"DVDD",
	"CPVDD",
};

struct zpcm512x_priv {
	struct regmap *regmap;
	struct clk *sclk;
	struct regulator_bulk_data supplies[PCM512x_NUM_SUPPLIES];
	struct notifier_block supply_nb[PCM512x_NUM_SUPPLIES];
	int fmt;
	int pll_in;
	int pll_out;
	int pll_r;
	int pll_j;
	int pll_d;
	int pll_p;
	unsigned long real_pll;
	unsigned long overclock_pll;
	unsigned long overclock_dac;
	unsigned long overclock_dsp;
	int mute;
	struct mutex mutex;
	unsigned int bclk_ratio;
	struct gpio_desc *mute_gpio;
	bool auto_gpio_mute;
	bool disable_pwrdown;
	bool disable_standby;
};

/*
 * We can't use the same notifier block for more than one supply and
 * there's no way I can see to get from a callback to the caller
 * except container_of().
 */
#define PCM512x_REGULATOR_EVENT(n) \
static int zpcm512x_regulator_event_##n(struct notifier_block *nb, \
					unsigned long event, void *data) \
{ \
	struct zpcm512x_priv *zpcm512x = \
			container_of(nb, struct zpcm512x_priv, supply_nb[n]); \
	if (event & REGULATOR_EVENT_DISABLE) { \
		regcache_mark_dirty(zpcm512x->regmap);	\
		regcache_cache_only(zpcm512x->regmap, true); \
	} \
	return 0; \
}

PCM512x_REGULATOR_EVENT(0)
PCM512x_REGULATOR_EVENT(1)
PCM512x_REGULATOR_EVENT(2)

static const struct reg_default zpcm512x_reg_defaults[] = {
	{ PCM512x_RESET,             0x00 },
	{ PCM512x_POWER,             0x00 },
	{ PCM512x_MUTE,              0x00 },
	{ PCM512x_DSP,               0x00 },
	{ PCM512x_PLL_REF,           0x00 },
	{ PCM512x_DAC_REF,           0x00 },
	{ PCM512x_DAC_ROUTING,       0x11 },
	{ PCM512x_DSP_PROGRAM,       0x01 },
	{ PCM512x_CLKDET,            0x00 },
	{ PCM512x_AUTO_MUTE,         0x00 },
	{ PCM512x_ERROR_DETECT,      0x00 },
	{ PCM512x_DIGITAL_VOLUME_1,  0x00 },
	{ PCM512x_DIGITAL_VOLUME_2,  0x30 },
	{ PCM512x_DIGITAL_VOLUME_3,  0x30 },
	{ PCM512x_DIGITAL_MUTE_1,    0x22 },
	{ PCM512x_DIGITAL_MUTE_2,    0x00 },
	{ PCM512x_DIGITAL_MUTE_3,    0x07 },
	{ PCM512x_OUTPUT_AMPLITUDE,  0x00 },
	{ PCM512x_ANALOG_GAIN_CTRL,  0x00 },
	{ PCM512x_UNDERVOLTAGE_PROT, 0x00 },
	{ PCM512x_ANALOG_MUTE_CTRL,  0x00 },
	{ PCM512x_ANALOG_GAIN_BOOST, 0x00 },
	{ PCM512x_VCOM_CTRL_1,       0x00 },
	{ PCM512x_VCOM_CTRL_2,       0x01 },
	{ PCM512x_BCLK_LRCLK_CFG,    0x00 },
	{ PCM512x_MASTER_MODE,       0x7c },
	{ PCM512x_GPIO_DACIN,        0x00 },
	{ PCM512x_GPIO_PLLIN,        0x00 },
	{ PCM512x_SYNCHRONIZE,       0x10 },
	{ PCM512x_PLL_COEFF_0,       0x00 },
	{ PCM512x_PLL_COEFF_1,       0x00 },
	{ PCM512x_PLL_COEFF_2,       0x00 },
	{ PCM512x_PLL_COEFF_3,       0x00 },
	{ PCM512x_PLL_COEFF_4,       0x00 },
	{ PCM512x_DSP_CLKDIV,        0x00 },
	{ PCM512x_DAC_CLKDIV,        0x00 },
	{ PCM512x_NCP_CLKDIV,        0x00 },
	{ PCM512x_OSR_CLKDIV,        0x00 },
	{ PCM512x_MASTER_CLKDIV_1,   0x00 },
	{ PCM512x_MASTER_CLKDIV_2,   0x00 },
	{ PCM512x_FS_SPEED_MODE,     0x00 },
	{ PCM512x_IDAC_1,            0x01 },
	{ PCM512x_IDAC_2,            0x00 },
	{ PCM512x_PAGE000_REG121,    0x00 },
};

static bool zpcm512x_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PCM512x_RESET:
	case PCM512x_POWER:
	case PCM512x_MUTE:
	case PCM512x_PLL_EN:
	case PCM512x_SPI_MISO_FUNCTION:
	case PCM512x_DSP:
	case PCM512x_GPIO_EN:
	case PCM512x_BCLK_LRCLK_CFG:
	case PCM512x_DSP_GPIO_INPUT:
	case PCM512x_MASTER_MODE:
	case PCM512x_PLL_REF:
	case PCM512x_DAC_REF:
	case PCM512x_GPIO_DACIN:
	case PCM512x_GPIO_PLLIN:
	case PCM512x_SYNCHRONIZE:
	case PCM512x_PLL_COEFF_0:
	case PCM512x_PLL_COEFF_1:
	case PCM512x_PLL_COEFF_2:
	case PCM512x_PLL_COEFF_3:
	case PCM512x_PLL_COEFF_4:
	case PCM512x_DSP_CLKDIV:
	case PCM512x_DAC_CLKDIV:
	case PCM512x_NCP_CLKDIV:
	case PCM512x_OSR_CLKDIV:
	case PCM512x_MASTER_CLKDIV_1:
	case PCM512x_MASTER_CLKDIV_2:
	case PCM512x_FS_SPEED_MODE:
	case PCM512x_IDAC_1:
	case PCM512x_IDAC_2:
	case PCM512x_ERROR_DETECT:
	case PCM512x_I2S_1:
	case PCM512x_I2S_2:
	case PCM512x_DAC_ROUTING:
	case PCM512x_DSP_PROGRAM:
	case PCM512x_CLKDET:
	case PCM512x_AUTO_MUTE:
	case PCM512x_DIGITAL_VOLUME_1:
	case PCM512x_DIGITAL_VOLUME_2:
	case PCM512x_DIGITAL_VOLUME_3:
	case PCM512x_DIGITAL_MUTE_1:
	case PCM512x_DIGITAL_MUTE_2:
	case PCM512x_DIGITAL_MUTE_3:
	case PCM512x_GPIO_OUTPUT_1:
	case PCM512x_GPIO_OUTPUT_2:
	case PCM512x_GPIO_OUTPUT_3:
	case PCM512x_GPIO_OUTPUT_4:
	case PCM512x_GPIO_OUTPUT_5:
	case PCM512x_GPIO_OUTPUT_6:
	case PCM512x_GPIO_CONTROL_1:
	case PCM512x_GPIO_CONTROL_2:
	case PCM512x_OVERFLOW:
	case PCM512x_RATE_DET_1:
	case PCM512x_RATE_DET_2:
	case PCM512x_RATE_DET_3:
	case PCM512x_RATE_DET_4:
	case PCM512x_CLOCK_STATUS:
	case PCM512x_ANALOG_MUTE_DET:
	case PCM512x_GPIN:
	case PCM512x_DIGITAL_MUTE_DET:
	case PCM512x_PAGE000_REG121:
	case PCM512x_OUTPUT_AMPLITUDE:
	case PCM512x_ANALOG_GAIN_CTRL:
	case PCM512x_UNDERVOLTAGE_PROT:
	case PCM512x_ANALOG_MUTE_CTRL:
	case PCM512x_ANALOG_GAIN_BOOST:
	case PCM512x_VCOM_CTRL_1:
	case PCM512x_VCOM_CTRL_2:
	case PCM512x_CRAM_CTRL:
	case PCM512x_FLEX_A:
	case PCM512x_FLEX_B:
		return true;
	default:
		/* There are 256 raw register addresses */
		return reg < 0xff;
	}
}

static bool zpcm512x_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PCM512x_PLL_EN:
	case PCM512x_OVERFLOW:
	case PCM512x_RATE_DET_1:
	case PCM512x_RATE_DET_2:
	case PCM512x_RATE_DET_3:
	case PCM512x_RATE_DET_4:
	case PCM512x_CLOCK_STATUS:
	case PCM512x_ANALOG_MUTE_DET:
	case PCM512x_GPIN:
	case PCM512x_DIGITAL_MUTE_DET:
	case PCM512x_CRAM_CTRL:
		return true;
	default:
		/* There are 256 raw register addresses */
		return reg < 0xff;
	}
}

static int zpcm512x_overclock_pll_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
				snd_soc_kcontrol_component(kcontrol);
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);
#ifdef DDDEBUG
	dev_dbg(component->dev, "%s: ENTER\n", __func__);
#endif
	ucontrol->value.integer.value[0] = zpcm512x->overclock_pll;
#ifdef DDDEBUG
	dev_dbg(component->dev, "%s: EXIT [0]\n", __func__);
#endif
	return 0;
}

static int zpcm512x_overclock_pll_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
				snd_soc_kcontrol_component(kcontrol);
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);
#ifdef DDDEBUG
	dev_dbg(component->dev, "%s: ENTER\n", __func__);
#endif
	switch (snd_soc_component_get_bias_level(component)) {
	case SND_SOC_BIAS_OFF:
	case SND_SOC_BIAS_STANDBY:
		break;
	default:
		dev_err(component->dev, "%s: EXIT [-EBUSY]\n", __func__);
		return -EBUSY;
	}

	zpcm512x->overclock_pll = ucontrol->value.integer.value[0];
#ifdef DDDEBUG
	dev_dbg(component->dev, "%s: EXIT [0]\n", __func__);
#endif
	return 0;
}

static int zpcm512x_overclock_dsp_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
				snd_soc_kcontrol_component(kcontrol);
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);
#ifdef DDDEBUG
	dev_dbg(component->dev, "%s: ENTER\n", __func__);
#endif
	ucontrol->value.integer.value[0] = zpcm512x->overclock_dsp;
#ifdef DDDEBUG	
	dev_dbg(component->dev, "%s: EXIT [0]\n", __func__);
#endif
	return 0;
}

static int zpcm512x_overclock_dsp_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
				snd_soc_kcontrol_component(kcontrol);
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);
#ifdef DDDEBUG
	dev_dbg(component->dev, "%s: ENTER\n", __func__);
#endif
	switch (snd_soc_component_get_bias_level(component)) {
	case SND_SOC_BIAS_OFF:
	case SND_SOC_BIAS_STANDBY:
		break;
	default:
		dev_err(component->dev, "%s: EXIT [-EBUSY]\n", __func__);
		return -EBUSY;
	}

	zpcm512x->overclock_dsp = ucontrol->value.integer.value[0];
#ifdef DDDEBUG
	dev_dbg(component->dev, "%s: EXIT [0]\n", __func__);
#endif
	return 0;
}

static int zpcm512x_overclock_dac_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
				snd_soc_kcontrol_component(kcontrol);
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);
#ifdef DDDEBUG
	dev_dbg(component->dev, "%s: ENTER\n", __func__);
#endif
	ucontrol->value.integer.value[0] = zpcm512x->overclock_dac;
#ifdef DDDEBUG
	dev_dbg(component->dev, "%s: EXIT [0]\n", __func__);
#endif
	return 0;
}

static int zpcm512x_overclock_dac_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
				snd_soc_kcontrol_component(kcontrol);
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);
#ifdef DDDEBUG
	dev_dbg(component->dev, "%s: ENTER\n", __func__);
#endif
	switch (snd_soc_component_get_bias_level(component)) {
	case SND_SOC_BIAS_OFF:
	case SND_SOC_BIAS_STANDBY:
		break;
	default:
		dev_err(component->dev, "%s: EXIT [-EBUSY]\n", __func__);
		return -EBUSY;
	}

	zpcm512x->overclock_dac = ucontrol->value.integer.value[0];
#ifdef DDDEBUG
	dev_dbg(component->dev, "%s: EXIT [0]\n", __func__);
#endif
	return 0;
}

static const DECLARE_TLV_DB_SCALE(digital_tlv, -10350, 50, 1);
static const DECLARE_TLV_DB_SCALE(analog_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(boost_tlv, 0, 80, 0);

static const char * const zpcm512x_dsp_program_texts[] = {
	"FIR interpolation with de-emphasis",
	"Low latency IIR with de-emphasis",
	"High attenuation with de-emphasis",
	"Fixed process flow",
	"Ringing-less low latency FIR",
};

static const unsigned int zpcm512x_dsp_program_values[] = {
	1,
	2,
	3,
	5,
	7,
};

static SOC_VALUE_ENUM_SINGLE_DECL(zpcm512x_dsp_program,
				  PCM512x_DSP_PROGRAM, 0, 0x1f,
				  zpcm512x_dsp_program_texts,
				  zpcm512x_dsp_program_values);

static const char * const zpcm512x_clk_missing_text[] = {
	"1s", "2s", "3s", "4s", "5s", "6s", "7s", "8s"
};

static const struct soc_enum zpcm512x_clk_missing =
	SOC_ENUM_SINGLE(PCM512x_CLKDET, 0,  8, zpcm512x_clk_missing_text);

static const char * const zpcm512x_autom_text[] = {
	"21ms", "106ms", "213ms", "533ms", "1.07s", "2.13s", "5.33s", "10.66s"
};

static const struct soc_enum zpcm512x_autom_l =
	SOC_ENUM_SINGLE(PCM512x_AUTO_MUTE, PCM512x_ATML_SHIFT, 8,
			zpcm512x_autom_text);

static const struct soc_enum zpcm512x_autom_r =
	SOC_ENUM_SINGLE(PCM512x_AUTO_MUTE, PCM512x_ATMR_SHIFT, 8,
			zpcm512x_autom_text);

static const char * const zpcm512x_ramp_rate_text[] = {
	"1 sample/update", "2 samples/update", "4 samples/update",
	"Immediate"
};

static const struct soc_enum zpcm512x_vndf =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_1, PCM512x_VNDF_SHIFT, 4,
			zpcm512x_ramp_rate_text);

static const struct soc_enum zpcm512x_vnuf =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_1, PCM512x_VNUF_SHIFT, 4,
			zpcm512x_ramp_rate_text);

static const struct soc_enum zpcm512x_vedf =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_2, PCM512x_VEDF_SHIFT, 4,
			zpcm512x_ramp_rate_text);

static const char * const zpcm512x_ramp_step_text[] = {
	"4dB/step", "2dB/step", "1dB/step", "0.5dB/step"
};

static const struct soc_enum zpcm512x_vnds =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_1, PCM512x_VNDS_SHIFT, 4,
			zpcm512x_ramp_step_text);

static const struct soc_enum zpcm512x_vnus =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_1, PCM512x_VNUS_SHIFT, 4,
			zpcm512x_ramp_step_text);

static const struct soc_enum zpcm512x_veds =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_2, PCM512x_VEDS_SHIFT, 4,
			zpcm512x_ramp_step_text);
/* DAMD (dac mode) */
static const char * const zpcm512x_dac_mode_texts[] = {
	/* Mode1 - New hyper-advanced current-segment architecture */
	"Hyper",
	/* Mode2 - Classic PCM1792 advanced current-segment architecture */
	"Classic"
};
static SOC_ENUM_SINGLE_DECL(zpcm512x_dac_mode_enum, /* name */
			    PCM512x_PAGE000_REG121_DAMD, /* xreg */
			    PCM512x_PAGE000_REG121_DAMD_SHIFT, /* xshift */
			    zpcm512x_dac_mode_texts /* xtexts */ );

static int zpcm512x_update_mute(struct snd_soc_component *component)
{
	int ret;
	struct device *dev = component->dev;
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);
	unsigned int val = (!!(zpcm512x->mute & 0x5) << PCM512x_RQML_SHIFT)
			    | (!!(zpcm512x->mute & 0x3) << PCM512x_RQMR_SHIFT);
	char *val_log = (val == (PCM512x_RQML | PCM512x_RQMR) ? "LEFT|RIGHT"
			 : (val == PCM512x_RQML) ? "LEFT"
			 : (val == PCM512x_RQMR) ? "RIGHT"
			 : (!val) ? "0FF" : "????");

	dev_dbg(dev, "%s: ENTER\n", __func__);

	dev_dbg(dev, "%s: set PCM512x_MUTE=%s\n", __func__, val_log);
	ret = regmap_update_bits(zpcm512x->regmap, PCM512x_MUTE,
				 PCM512x_RQML | PCM512x_RQMR, val);
	if (ret < 0) {
		dev_err(dev, "%s: EXIT [%d]: set PCM512x_MUTE=%s returns: "
			"[%d]\n", __func__, ret, val_log, ret);
		return ret;
	}

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static int zpcm512x_digital_playback_switch_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
				snd_soc_kcontrol_component(kcontrol);
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);
#ifdef DDDEBUG
	dev_dbg(component->dev, "%s: ENTER\n", __func__);
#endif
	mutex_lock(&zpcm512x->mutex);

	ucontrol->value.integer.value[0] = !(zpcm512x->mute & 0x4);
	ucontrol->value.integer.value[1] = !(zpcm512x->mute & 0x2);

	mutex_unlock(&zpcm512x->mutex);
#ifdef DDDEBUG
	dev_dbg(component->dev, "%s: EXIT [0]\n", __func__);
#endif
	return 0;
}

static int zpcm512x_digital_playback_switch_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
				snd_soc_kcontrol_component(kcontrol);
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);
	int ret, changed = 0;
#ifdef DDDEBUG
	dev_dbg(component->dev, "%s: ENTER\n", __func__);
#endif
	mutex_lock(&zpcm512x->mutex);

	if ((zpcm512x->mute & 0x4) == (ucontrol->value.integer.value[0] << 2)) {
		zpcm512x->mute ^= 0x4;
		changed = 1;
	}
	if ((zpcm512x->mute & 0x2) == (ucontrol->value.integer.value[1] << 1)) {
		zpcm512x->mute ^= 0x2;
		changed = 1;
	}

	if (changed) {
		ret = zpcm512x_update_mute(component);
		if (ret != 0) {
			dev_err(component->dev, "%s: EXIT [%d]: failed to "
				"update digital mute!\n", __func__, ret);
			mutex_unlock(&zpcm512x->mutex);
			return ret;
		}
	}

	mutex_unlock(&zpcm512x->mutex);
#ifdef DDDEBUG
	dev_dbg(component->dev, "%s: EXIT [%d]\n", __func__, changed);
#endif
	return changed;
}

static const struct snd_kcontrol_new zpcm512x_controls[] = {
SOC_DOUBLE_R_TLV("Digital Playback Volume", PCM512x_DIGITAL_VOLUME_2,
		 PCM512x_DIGITAL_VOLUME_3, 0, 255, 1, digital_tlv),
SOC_DOUBLE_TLV("Analogue Playback Volume", PCM512x_ANALOG_GAIN_CTRL,
	       PCM512x_LAGN_SHIFT, PCM512x_RAGN_SHIFT, 1, 1, analog_tlv),
SOC_DOUBLE_TLV("Analogue Playback Boost Volume", PCM512x_ANALOG_GAIN_BOOST,
	       PCM512x_AGBL_SHIFT, PCM512x_AGBR_SHIFT, 1, 0, boost_tlv),
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Digital Playback Switch",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = snd_ctl_boolean_stereo_info,
	.get = zpcm512x_digital_playback_switch_get,
	.put = zpcm512x_digital_playback_switch_put
},

SOC_SINGLE("Deemphasis Switch", PCM512x_DSP, PCM512x_DEMP_SHIFT, 1, 1),
SOC_ENUM("DSP Program", zpcm512x_dsp_program),

SOC_ENUM("Clock Missing Period", zpcm512x_clk_missing),
SOC_ENUM("Auto Mute Time Left", zpcm512x_autom_l),
SOC_ENUM("Auto Mute Time Right", zpcm512x_autom_r),
SOC_SINGLE("Auto Mute Mono Switch", PCM512x_DIGITAL_MUTE_3,
	   PCM512x_ACTL_SHIFT, 1, 0),
SOC_DOUBLE("Auto Mute Switch", PCM512x_DIGITAL_MUTE_3, PCM512x_AMLE_SHIFT,
	   PCM512x_AMRE_SHIFT, 1, 0),

SOC_ENUM("Volume Ramp Down Rate", zpcm512x_vndf),
SOC_ENUM("Volume Ramp Down Step", zpcm512x_vnds),
SOC_ENUM("Volume Ramp Up Rate", zpcm512x_vnuf),
SOC_ENUM("Volume Ramp Up Step", zpcm512x_vnus),
SOC_ENUM("Volume Ramp Down Emergency Rate", zpcm512x_vedf),
SOC_ENUM("Volume Ramp Down Emergency Step", zpcm512x_veds),

SOC_SINGLE_EXT("Max Overclock PLL", SND_SOC_NOPM, 0, 20, 0,
	       zpcm512x_overclock_pll_get, zpcm512x_overclock_pll_put),
SOC_SINGLE_EXT("Max Overclock DSP", SND_SOC_NOPM, 0, 40, 0,
	       zpcm512x_overclock_dsp_get, zpcm512x_overclock_dsp_put),
SOC_SINGLE_EXT("Max Overclock DAC", SND_SOC_NOPM, 0, 40, 0,
	       zpcm512x_overclock_dac_get, zpcm512x_overclock_dac_put),
/* DAMD (dac mode) - hyper-advanced or classic PCM1792 */
SOC_ENUM("DAC Mode", zpcm512x_dac_mode_enum),
};

static const struct snd_soc_dapm_widget zpcm512x_dapm_widgets[] = {
SND_SOC_DAPM_DAC("DACL", NULL, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_DAC("DACR", NULL, SND_SOC_NOPM, 0, 0),

SND_SOC_DAPM_OUTPUT("OUTL"),
SND_SOC_DAPM_OUTPUT("OUTR"),
};

static const struct snd_soc_dapm_route zpcm512x_dapm_routes[] = {
	{ "DACL", NULL, "Playback" },
	{ "DACR", NULL, "Playback" },

	{ "OUTL", NULL, "DACL" },
	{ "OUTR", NULL, "DACR" },
};

static unsigned long zpcm512x_pll_max_(struct zpcm512x_priv *zpcm512x)
{
	return 25000000 + 25000000 * zpcm512x->overclock_pll / 100;
}

static unsigned long zpcm512x_pll_max(struct snd_soc_component *component)
{
	unsigned long pll_max;
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "%s: ENTER\n", __func__);

	pll_max = zpcm512x_pll_max_(zpcm512x);

	dev_dbg(component->dev, "%s: EXIT [%lu]\n", __func__, pll_max);	
	return pll_max;
}

static unsigned long zpcm512x_dsp_max(struct snd_soc_component *component)
{
	unsigned long dsp_max;
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "%s: ENTER\n", __func__);
	
	dsp_max = 50000000 + 50000000 * zpcm512x->overclock_dsp / 100;

	dev_dbg(component->dev, "%s: EXIT [%lu]\n", __func__, dsp_max);
	return dsp_max;
}

static unsigned long zpcm512x_dac_max(struct snd_soc_component *component,
				      unsigned long rate)
{
	unsigned long dac_max;
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "%s: ENTER\n", __func__);

	dac_max = rate + rate * zpcm512x->overclock_dac / 100;

	dev_dbg(component->dev, "%s: EXIT [%lu]\n", __func__, dac_max);
	return dac_max;
}

static unsigned long zpcm512x_sck_max(struct zpcm512x_priv *zpcm512x)
{
	if (!zpcm512x->pll_out)
		return 25000000;

	return zpcm512x_pll_max_(zpcm512x);
}

static unsigned long zpcm512x_ncp_target(struct snd_soc_component *component,
					 unsigned long dac_rate)
{
	unsigned long ncp_target;

	dev_dbg(component->dev, "%s: ENTER\n", __func__);

	/*
	 * If the DAC is not actually overclocked, use the good old
	 * NCP target rate...
	 */
	if (dac_rate <= 6144000)
		ncp_target = 1536000;
	/*
	 * ...but if the DAC is in fact overclocked, bump the NCP target
	 * rate to get the recommended dividers even when overclocking.
	 */
	else
		ncp_target = zpcm512x_dac_max(component, 1536000);

	dev_dbg(component->dev, "%s: EXIT [%lu]\n", __func__, ncp_target);
	return ncp_target;
}

static const char zpcm512x_dai_rates_texts[] =
	"8k,11k025,16k,22k050,32k,44k1,48k,64k,88k2,96k,176k4,192k,352k8,384k";

static const u32 zpcm512x_dai_rates[] = {
	8000, 11025, 16000, 22050, 32000, 44100, 48000, 64000,
	88200, 96000, 176400, 192000, 352800, 384000,
};

static const struct snd_pcm_hw_constraint_list constraints_slave = {
	.count = ARRAY_SIZE(zpcm512x_dai_rates),
	.list  = zpcm512x_dai_rates,
};

static int zpcm512x_hw_rule_rate(struct snd_pcm_hw_params *params,
				 struct snd_pcm_hw_rule *rule)
{
	struct zpcm512x_priv *zpcm512x = rule->private;
	struct snd_interval ranges[2];
	int frame_size;

	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0)
		return frame_size;

	switch (frame_size) {
	case 32:
		/* No hole when the frame size is 32. */
		return 0;
	case 48:
	case 64:
		/* There is only one hole in the range of supported
		 * rates, but it moves with the frame size.
		 */
		memset(ranges, 0, sizeof(ranges));
		ranges[0].min = 8000;
		ranges[0].max = zpcm512x_sck_max(zpcm512x) / frame_size / 2;
		ranges[1].min = DIV_ROUND_UP(16000000, frame_size);
		ranges[1].max = 384000;
		break;
	default:
		return -EINVAL;
	}

	return snd_interval_ranges(hw_param_interval(params, rule->var),
				   ARRAY_SIZE(ranges), ranges, 0);
}

static int zpcm512x_dai_startup_master(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct zpcm512x_priv *zpcm512x = snd_soc_component_get_drvdata(component);
	struct device *dev = dai->dev;
	struct snd_pcm_hw_constraint_ratnums *constraints_no_pll;
	struct snd_ratnum *rats_no_pll;
	int ret;

	dev_dbg(component->dev, "%s: ENTER\n", __func__);

	if (IS_ERR(zpcm512x->sclk)) {
		dev_err(dev, "%s: EXIT [%ld]: need SCLK for master mode!\n",
			__func__, PTR_ERR(zpcm512x->sclk));
		return PTR_ERR(zpcm512x->sclk);
	}

	if (zpcm512x->pll_out) {
		ret = snd_pcm_hw_rule_add(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  zpcm512x_hw_rule_rate,
					  zpcm512x,
					  SNDRV_PCM_HW_PARAM_FRAME_BITS,
					  SNDRV_PCM_HW_PARAM_CHANNELS, -1);

		dev_dbg(component->dev, "%s: EXIT [%d]\n", __func__, ret);
		return ret;
	}

	constraints_no_pll = devm_kzalloc(dev, sizeof(*constraints_no_pll),
					  GFP_KERNEL);
	if (!constraints_no_pll) {
		dev_err(component->dev, "%s: EXIT [-ENOMEM]: "
			"constraints_no_pll devm_kzalloc error\n", __func__);
		return -ENOMEM;
	}

	constraints_no_pll->nrats = 1;
	rats_no_pll = devm_kzalloc(dev, sizeof(*rats_no_pll), GFP_KERNEL);
	if (!rats_no_pll) {
		dev_err(component->dev, "%s: EXIT [-ENOMEM]: rats_no_pll "
			"devm_kzalloc error\n", __func__);
		return -ENOMEM;
	}

	constraints_no_pll->rats = rats_no_pll;
	rats_no_pll->num = clk_get_rate(zpcm512x->sclk) / 64;
	rats_no_pll->den_min = 1;
	rats_no_pll->den_max = 128;
	rats_no_pll->den_step = 1;

	dev_dbg(component->dev, "%s: set ratnums constraint: num=%d, "
		"den_min=%d, den_max=%d, den_step=%d\n", __func__,
		rats_no_pll->num, rats_no_pll->den_min, rats_no_pll->den_max,
		rats_no_pll->den_step);

	ret = snd_pcm_hw_constraint_ratnums(substream->runtime, 0,
					    SNDRV_PCM_HW_PARAM_RATE,
					    constraints_no_pll);

	dev_dbg(component->dev, "%s: EXIT [%d]\n", __func__, ret);
	return ret;
}

static int zpcm512x_dai_startup_slave(struct snd_pcm_substream *substream,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);
	struct device *dev = dai->dev;
	struct regmap *regmap = zpcm512x->regmap;
	int ret;

	dev_dbg(component->dev, "%s: ENTER\n", __func__);

	if (IS_ERR(zpcm512x->sclk)) {
		dev_dbg(dev, "%s: no SCLK, using BCLK: %ld\n",
			__func__, PTR_ERR(zpcm512x->sclk));

		/* Disable reporting of missing SCLK as an error */
		regmap_update_bits(regmap, PCM512x_ERROR_DETECT,
				   PCM512x_IDCH, PCM512x_IDCH);

		/* Switch PLL input to BCLK */
		regmap_update_bits(regmap, PCM512x_PLL_REF,
				   PCM512x_SREF, PCM512x_SREF_BCK);
	}

	dev_dbg(component->dev, "%s: set slave rates (%s) constraint\n",
		__func__, zpcm512x_dai_rates_texts);

	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_RATE,
					 &constraints_slave);

	dev_dbg(component->dev, "%s: EXIT [%d]\n", __func__, ret);
	return ret;
}

static int zpcm512x_dai_startup(struct snd_pcm_substream *substream,
			        struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct zpcm512x_priv *zpcm512x = snd_soc_component_get_drvdata(component);
	int ret;

	dev_dbg(component->dev, "%s: ENTER\n", __func__);

	switch (zpcm512x->fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
	case SND_SOC_DAIFMT_CBM_CFS:
		ret = zpcm512x_dai_startup_master(substream, dai);
		dev_dbg(component->dev, "%s: EXIT [%d]\n", __func__, ret);
		return ret;
	case SND_SOC_DAIFMT_CBS_CFS:
		ret = zpcm512x_dai_startup_slave(substream, dai);
		dev_dbg(component->dev, "%s: EXIT [%d]\n", __func__, ret);
		return ret;
	default:
		dev_err(component->dev, "%s: EXIT [-EINVAL]: Invalid DAIFMT!\n",
			__func__);
		return -EINVAL;
	}
}

static char* zpcm512x_log_bias_level(enum snd_soc_bias_level level)
{
	switch (level) {
		case SND_SOC_BIAS_OFF: return "SND_SOC_BIAS_OFF";
		case SND_SOC_BIAS_STANDBY: return "SND_SOC_BIAS_STANDBY";
		case SND_SOC_BIAS_PREPARE: return "SND_SOC_BIAS_PREPARE";
		case SND_SOC_BIAS_ON: return "SND_SOC_BIAS_ON";
		default: return "UNKNOWN";
	}
}

static int zpcm512x_set_bias_level(struct snd_soc_component *component,
				   enum snd_soc_bias_level level)
{
	struct zpcm512x_priv *zpcm512x = dev_get_drvdata(component->dev);
	int ret;

	dev_dbg(component->dev, "%s: ENTER: level=%s\n", __func__,
		zpcm512x_log_bias_level(level));

	if (zpcm512x->disable_standby) {
		dev_dbg(component->dev, "%s: EXIT [0]: noop - ignoring because "
			"RQST standby is disabled\n", __func__);
		return 0;
	}

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) ==
							SND_SOC_BIAS_OFF) {
#ifdef DDEBUG
			dev_dbg(component->dev, "%s: set RQST to normal "
				"operation\n", __func__);
#endif /* DDEBUG */
			ret = regmap_update_bits(zpcm512x->regmap,
						 PCM512x_POWER, PCM512x_RQST,
						 0);
			if (ret != 0) {
				dev_err(component->dev, "%s: EXIT [%d]: failed "
					"setting RQST to normal operation!\n",
					__func__, ret);
				return ret;
			}
		}
		break;
	case SND_SOC_BIAS_OFF:
#ifdef DDEBUG
		dev_dbg(component->dev, "%s: set RQST to standby mode\n",
			__func__);
#endif
		ret = regmap_update_bits(zpcm512x->regmap, PCM512x_POWER,
					 PCM512x_RQST, PCM512x_RQST);
		if (ret != 0) {
			dev_err(component->dev, "%s: EXIT [%d]: failed setting "
				"RQST to standby mode!\n", __func__, ret);
			return ret;
		}
		break;
	}

	dev_dbg(component->dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static unsigned long zpcm512x_find_sck(struct snd_soc_dai *dai,
				       unsigned long bclk_rate)
{
	struct device *dev = dai->dev;
	struct snd_soc_component *component = dai->component;
	unsigned long sck_rate;
	int pow2;

	dev_dbg(dev, "%s: ENTER: bclk_rate=%lu\n", __func__, bclk_rate);

	/* 64 MHz <= pll_rate <= 100 MHz, VREF mode */
	/* 16 MHz <= sck_rate <=  25 MHz, VREF mode */

	/* select sck_rate as a multiple of bclk_rate but still with
	 * as many factors of 2 as possible, as that makes it easier
	 * to find a fast DAC rate
	 */
	pow2 = 1 << fls((zpcm512x_pll_max(component) - 16000000) / bclk_rate);
	for (; pow2; pow2 >>= 1) {
		sck_rate = rounddown(zpcm512x_pll_max(component),
				     bclk_rate * pow2);
		if (sck_rate >= 16000000)
			break;
	}
	if (!pow2) {
		dev_err(dev, "%s: EXIT [0]: impossible to generate a suitable "
			"SCK!\n", __func__);
		return 0;
	}

	dev_dbg(dev, "%s: EXIT [%lu]\n", __func__, sck_rate);
	return sck_rate;
}

/* pll_rate = pllin_rate * R * J.D / P
 * 1 <= R <= 16
 * 1 <= J <= 63
 * 0 <= D <= 9999
 * 1 <= P <= 15
 * 64 MHz <= pll_rate <= 100 MHz
 * if D == 0
 *     1 MHz <= pllin_rate / P <= 20 MHz
 * else if D > 0
 *     6.667 MHz <= pllin_rate / P <= 20 MHz
 *     4 <= J <= 11
 *     R = 1
 */
static int zpcm512x_find_pll_coeff(struct snd_soc_dai *dai,
				   unsigned long pllin_rate,
				   unsigned long pll_rate)
{
	struct device *dev = dai->dev;
	struct snd_soc_component *component = dai->component;
	struct zpcm512x_priv *zpcm512x = snd_soc_component_get_drvdata(component);
	unsigned long common;
	int R, J, D, P;
	unsigned long K; /* 10000 * J.D */
	unsigned long num;
	unsigned long den;

	dev_dbg(dev, "%s: ENTER: pllin_rate=%lu, pll_rate=%lu\n", __func__,
		pllin_rate, pll_rate);

	common = gcd(pll_rate, pllin_rate);
	num = pll_rate / common;
	den = pllin_rate / common;

	/* pllin_rate / P (or here, den) cannot be greater than 20 MHz */
	if (pllin_rate / den > 20000000 && num < 8) {
		num *= DIV_ROUND_UP(pllin_rate / den, 20000000);
		den *= DIV_ROUND_UP(pllin_rate / den, 20000000);
	}
	dev_dbg(dev, "%s: num=%lu, den=%lu, common=%lu\n",
		__func__, num, den, common);

	P = den;
	if (den <= 15 && num <= 16 * 63
	    && 1000000 <= pllin_rate / P && pllin_rate / P <= 20000000) {
		/* Try the case with D = 0 */
		D = 0;
		/* factor 'num' into J and R, such that R <= 16 and J <= 63 */
		for (R = 16; R; R--) {
			if (num % R)
				continue;
			J = num / R;
			if (J == 0 || J > 63)
				continue;

			dev_dbg(dev, "%s: R * J / P = %d * %d / %d\n",
				__func__, R, J, P);
			zpcm512x->real_pll = pll_rate;
			goto done;
		}
		/* no luck */
	}

	R = 1;

	if (num > 0xffffffffUL / 10000)
		goto fallback;

	/* Try to find an exact pll_rate using the D > 0 case */
	common = gcd(10000 * num, den);
	num = 10000 * num / common;
	den /= common;
	dev_dbg(dev, "%s: num=%lu, den=%lu, common=%lu\n",
		__func__, num, den, common);

	for (P = den; P <= 15; P++) {
		if (pllin_rate / P < 6667000 || 200000000 < pllin_rate / P)
			continue;
		if (num * P % den)
			continue;
		K = num * P / den;
		/* J == 12 is ok if D == 0 */
		if (K < 40000 || K > 120000)
			continue;

		J = K / 10000;
		D = K % 10000;
		dev_dbg(dev, "%s: J.D / P = %d.%04d / %d\n", __func__, J, D, P);
		zpcm512x->real_pll = pll_rate;
		goto done;
	}

	/* Fall back to an approximate pll_rate */

fallback:
	/* find smallest possible P */
	P = DIV_ROUND_UP(pllin_rate, 20000000);
	if (!P)
		P = 1;
	else if (P > 15) {
		dev_err(dev, "%s: EXIT [-EINVAL]: need a slower clock as "
			"pll-input!\n", __func__);
		return -EINVAL;
	}
	if (pllin_rate / P < 6667000) {
		dev_err(dev, "%s: EXIT [-EINVAL]: need a faster clock as "
			"pll-input!\n", __func__);
		return -EINVAL;
	}
	K = DIV_ROUND_CLOSEST_ULL(10000ULL * pll_rate * P, pllin_rate);
	if (K < 40000)
		K = 40000;
	/* J == 12 is ok if D == 0 */
	if (K > 120000)
		K = 120000;
	J = K / 10000;
	D = K % 10000;
	dev_dbg(dev, "%s: J.D / P ~ %d.%04d / %d\n", __func__, J, D, P);
	zpcm512x->real_pll = DIV_ROUND_DOWN_ULL((u64)K * pllin_rate, 10000 * P);

done:
	zpcm512x->pll_r = R;
	zpcm512x->pll_j = J;
	zpcm512x->pll_d = D;
	zpcm512x->pll_p = P;

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static unsigned long zpcm512x_pllin_dac_rate(struct snd_soc_dai *dai,
					     unsigned long osr_rate,
					     unsigned long pllin_rate)
{
	struct snd_soc_component *component = dai->component;
	struct zpcm512x_priv *zpcm512x = snd_soc_component_get_drvdata(component);
	unsigned long dac_rate;

	dev_dbg(component->dev, "%s: ENTER: osr_rate=%lu, pllin_rate=%lu\n",
		__func__, osr_rate, pllin_rate);

	if (!zpcm512x->pll_out) {
		dev_dbg(component->dev, "%s: EXIT [0]: no PLL to bypass, force "
			"SCK as DAC input\n", __func__);
		return 0; /* no PLL to bypass, force SCK as DAC input */
	}

	if (pllin_rate % osr_rate) {
		dev_dbg(component->dev, "%s: EXIT [0]: futile, quit early\n",
			__func__);
		return 0; /* futile, quit early */
	}

	/* run DAC no faster than 6144000 Hz */
	for (dac_rate = rounddown(zpcm512x_dac_max(component, 6144000),
				  osr_rate);
	     dac_rate;
	     dac_rate -= osr_rate) {

		if (pllin_rate / dac_rate > 128) {
			dev_dbg(component->dev, "%s: EXIT [0]: DAC divider "
				"would be too big\n", __func__);
			return 0; /* DAC divider would be too big */
		}

		if (!(pllin_rate % dac_rate)) {
			dev_dbg(component->dev, "%s: EXIT [%lu]\n", __func__,
				dac_rate);		
			return dac_rate;
		}

		dac_rate -= osr_rate;
	}

	dev_dbg(component->dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static int zpcm512x_set_dividers(struct snd_soc_dai *dai,
				 struct snd_pcm_hw_params *params)
{
	struct device *dev = dai->dev;
	struct snd_soc_component *component = dai->component;
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);
	unsigned long pllin_rate = 0;
	unsigned long pll_rate;
	unsigned long sck_rate;
	unsigned long mck_rate;
	unsigned long bclk_rate;
	unsigned long sample_rate;
	unsigned long osr_rate;
	unsigned long dacsrc_rate;
	int bclk_div;
	int lrclk_div;
	int dsp_div;
	int dac_div;
	unsigned long dac_rate;
	int ncp_div;
	int osr_div;
	int ret;
	int idac;
	int fssp;
	int gpio;

	dev_dbg(dev, "%s: ENTER\n", __func__);

	if (zpcm512x->bclk_ratio > 0) {
		lrclk_div = zpcm512x->bclk_ratio;
	} else {
		lrclk_div = snd_soc_params_to_frame_size(params);

		if (lrclk_div == 0) {
			dev_err(dev, "%s: EXIT [-EINVAL]: No LRCLK?\n",
				__func__);
			return -EINVAL;
		}
	}

	if (!zpcm512x->pll_out) {
		sck_rate = clk_get_rate(zpcm512x->sclk);
		bclk_rate = params_rate(params) * lrclk_div;
		bclk_div = DIV_ROUND_CLOSEST(sck_rate, bclk_rate);

		mck_rate = sck_rate;
	} else {
		ret = snd_soc_params_to_bclk(params);
		if (ret < 0) {
			dev_err(dev, "%s: EXIT [%d]: failed to find suitable "
				"BCLK!\n", __func__, ret);
			return ret;
		}
		if (ret == 0) {
			dev_err(dev, "%s: EXIT [-EINVAL]: no BCLK?\n",
				__func__);
			return -EINVAL;
		}
		bclk_rate = ret;

		pllin_rate = clk_get_rate(zpcm512x->sclk);

		sck_rate = zpcm512x_find_sck(dai, bclk_rate);
		if (!sck_rate) {
			dev_err(dev, "%s: EXIT [-EINVAL]: error finding "
				"SCLK!\n", __func__);
			return -EINVAL;
		}
		pll_rate = 4 * sck_rate;

		ret = zpcm512x_find_pll_coeff(dai, pllin_rate, pll_rate);
		if (ret != 0) {
			dev_err(dev, "%s: EXIT [%d]: error finding pll "
				"coeff!\n", __func__, ret);
			return ret;
		}

		ret = regmap_write(zpcm512x->regmap,
				   PCM512x_PLL_COEFF_0, zpcm512x->pll_p - 1);
		if (ret != 0) {
			dev_err(dev, "%s: EXIT [%d]: failed to write PLL P!\n",
				__func__, ret);
			return ret;
		}

		ret = regmap_write(zpcm512x->regmap,
				   PCM512x_PLL_COEFF_1, zpcm512x->pll_j);
		if (ret != 0) {
			dev_err(dev, "%s: EXIT [%d]: failed to write PLL J!\n",
				__func__, ret);
			return ret;
		}

		ret = regmap_write(zpcm512x->regmap,
				   PCM512x_PLL_COEFF_2, zpcm512x->pll_d >> 8);
		if (ret != 0) {
			dev_err(dev, "%s: EXIT [%d]: failed to write PLL D "
				"msb!\n", __func__, ret);
			return ret;
		}

		ret = regmap_write(zpcm512x->regmap,
				   PCM512x_PLL_COEFF_3, zpcm512x->pll_d & 0xff);
		if (ret != 0) {
			dev_err(dev, "%s: EXIT [%d]: failed to write PLL D "
				"lsb!\n", __func__, ret);
			return ret;
		}

		ret = regmap_write(zpcm512x->regmap,
				   PCM512x_PLL_COEFF_4, zpcm512x->pll_r - 1);
		if (ret != 0) {
			dev_err(dev, "%s: EXIT [%d]: failed to write PLL R!\n",
				__func__, ret);
			return ret;
		}

		mck_rate = zpcm512x->real_pll;

		bclk_div = DIV_ROUND_CLOSEST(sck_rate, bclk_rate);
	}

	if (bclk_div > 128) {
		dev_err(dev, "%s: EXIT [-EINVAL]: failed to find BCLK "
			"divider!\n", __func__);
		return -EINVAL;
	}

	/* the actual rate */
	sample_rate = sck_rate / bclk_div / lrclk_div;
	osr_rate = 16 * sample_rate;

	/* run DSP no faster than 50 MHz */
	dsp_div = mck_rate > zpcm512x_dsp_max(component) ? 2 : 1;

	dac_rate = zpcm512x_pllin_dac_rate(dai, osr_rate, pllin_rate);
	if (dac_rate) {
		/* the desired clock rate is "compatible" with the pll input
		 * clock, so use that clock as dac input instead of the pll
		 * output clock since the pll will introduce jitter and thus
		 * noise.
		 */
		dev_dbg(dev, "%s: using pll input as dac input\n", __func__);
		ret = regmap_update_bits(zpcm512x->regmap, PCM512x_DAC_REF,
					 PCM512x_SDAC, PCM512x_SDAC_GPIO);
		if (ret != 0) {
			dev_err(component->dev, "%s: EXIT [%d]: failed to set "
				"gpio as dacref!\n", __func__, ret);
			return ret;
		}

		gpio = PCM512x_GREF_GPIO1 + zpcm512x->pll_in - 1;
		ret = regmap_update_bits(zpcm512x->regmap, PCM512x_GPIO_DACIN,
					 PCM512x_GREF, gpio);
		if (ret != 0) {
			dev_err(component->dev, "%s: EXIT [%d]: failed to set "
				"gpio %d as dacin!\n", __func__, ret, 
				zpcm512x->pll_in);
			return ret;
		}

		dacsrc_rate = pllin_rate;
	} else {
		/* run DAC no faster than 6144000 Hz */
		unsigned long dac_mul = zpcm512x_dac_max(component, 6144000)
			/ osr_rate;
		unsigned long sck_mul = sck_rate / osr_rate;

		for (; dac_mul; dac_mul--) {
			if (!(sck_mul % dac_mul))
				break;
		}
		if (!dac_mul) {
			dev_err(dev, "%s: EXIT [-EINVAL]: failed to find DAC "
				"rate!\n", __func__);
			return -EINVAL;
		}

		dac_rate = dac_mul * osr_rate;

		dev_dbg(component->dev, "%s: dac_rate=%lu, sample_rate=%lu\n",
			__func__, dac_rate, sample_rate);

		ret = regmap_update_bits(zpcm512x->regmap, PCM512x_DAC_REF,
					 PCM512x_SDAC, PCM512x_SDAC_SCK);
		if (ret != 0) {
			dev_err(component->dev, "%s: EXIT [%d]: failed to set "
				"sck as dacref!\n", __func__, ret);
			return ret;
		}

		dacsrc_rate = sck_rate;
	}

	osr_div = DIV_ROUND_CLOSEST(dac_rate, osr_rate);
	if (osr_div > 128) {
		dev_err(dev, "%s: EXIT [-EINVAL]: failed to find OSR "
			"divider!\n", __func__);
		return -EINVAL;
	}

	dac_div = DIV_ROUND_CLOSEST(dacsrc_rate, dac_rate);
	if (dac_div > 128) {
		dev_err(dev, "%s: EXIT [-EINVAL]: failed to find DAC "
			"divider!\n", __func__);
		return -EINVAL;
	}
	dac_rate = dacsrc_rate / dac_div;

	ncp_div = DIV_ROUND_CLOSEST(dac_rate,
				    zpcm512x_ncp_target(component, dac_rate));
	if (ncp_div > 128 || dac_rate / ncp_div > 2048000) {
		/* run NCP no faster than 2048000 Hz, but why? */
		ncp_div = DIV_ROUND_UP(dac_rate, 2048000);
		if (ncp_div > 128) {
			dev_err(dev, "%s: EXIT [-EINVAL]: failed to find NCP "
				"divider!\n", __func__);
			return -EINVAL;
		}
	}

	idac = mck_rate / (dsp_div * sample_rate);

	ret = regmap_write(zpcm512x->regmap, PCM512x_DSP_CLKDIV, dsp_div - 1);
	if (ret != 0) {
		dev_err(dev, "%s: EXIT [%d]: failed to write DSP divider!\n",
			__func__, ret);
		return ret;
	}

	ret = regmap_write(zpcm512x->regmap, PCM512x_DAC_CLKDIV, dac_div - 1);
	if (ret != 0) {
		dev_err(dev, "%s: EXIT [%d]: failed to write DAC divider!\n",
			__func__, ret);
		return ret;
	}

	ret = regmap_write(zpcm512x->regmap, PCM512x_NCP_CLKDIV, ncp_div - 1);
	if (ret != 0) {
		dev_err(dev, "%s: EXIT [%d]: failed to write NCP divider!\n",
			__func__, ret);
		return ret;
	}

	ret = regmap_write(zpcm512x->regmap, PCM512x_OSR_CLKDIV, osr_div - 1);
	if (ret != 0) {
		dev_err(dev, "%s: EXIT [%d]: failed to write OSR divider!\n",
			__func__, ret);
		return ret;
	}

	ret = regmap_write(zpcm512x->regmap,
			   PCM512x_MASTER_CLKDIV_1, bclk_div - 1);
	if (ret != 0) {
		dev_err(dev, "%s: EXIT [%d]: failed to write BCLK divider!\n",
			__func__, ret);
		return ret;
	}

	ret = regmap_write(zpcm512x->regmap,
			   PCM512x_MASTER_CLKDIV_2, lrclk_div - 1);
	if (ret != 0) {
		dev_err(dev, "%s: EXIT [%d]: failed to write LRCLK divider!\n",
			__func__, ret);
		return ret;
	}

	ret = regmap_write(zpcm512x->regmap, PCM512x_IDAC_1, idac >> 8);
	if (ret != 0) {
		dev_err(dev, "%s: EXIT [%d]: failed to write IDAC msb "
			"divider!\n", __func__, ret);
		return ret;
	}

	ret = regmap_write(zpcm512x->regmap, PCM512x_IDAC_2, idac & 0xff);
	if (ret != 0) {
		dev_err(dev, "%s: EXIT [%d]: failed to write IDAC lsb "
			"divider!\n", __func__, ret);
		return ret;
	}

	if (sample_rate <= zpcm512x_dac_max(component, 48000))
		fssp = PCM512x_FSSP_48KHZ;
	else if (sample_rate <= zpcm512x_dac_max(component, 96000))
		fssp = PCM512x_FSSP_96KHZ;
	else if (sample_rate <= zpcm512x_dac_max(component, 192000))
		fssp = PCM512x_FSSP_192KHZ;
	else
		fssp = PCM512x_FSSP_384KHZ;
	ret = regmap_update_bits(zpcm512x->regmap, PCM512x_FS_SPEED_MODE,
				 PCM512x_FSSP, fssp);
	if (ret != 0) {
		dev_err(component->dev, "%s: EXIT [%d]: failed to set fs "
			"speed!\n", __func__, ret);
		return ret;
	}

	dev_dbg(component->dev, "%s: EXIT [0]: DSP div=%d, DAC div=%d, "
		"NCP div=%d, OSR div=%d, BCK div=%d, LRCK div=%d, IDAC=%d, "
		"1<<FSSP=%d\n", __func__, dsp_div, dac_div, ncp_div, osr_div,
		bclk_div, lrclk_div, idac, 1 << fssp);
	return 0;
}

static int zpcm512x_dai_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);
	int alen;
	int gpio;
	int ret;
	
	snd_pcm_format_t format = params_format(params);
        
	dev_dbg(component->dev, "%s: ENTER: frequency=%u, format=%s, "
		"sample_bits=%u, physical_bits=%u, channels=%u\n", __func__,
		params_rate(params), snd_pcm_format_name(format),
		snd_pcm_format_width(format),
		snd_pcm_format_physical_width(format),
		params_channels(params));

	switch (params_width(params)) {
	case 16:
		alen = PCM512x_ALEN_16;
		break;
	case 20:
		alen = PCM512x_ALEN_20;
		break;
	case 24:
		alen = PCM512x_ALEN_24;
		break;
	case 32:
		alen = PCM512x_ALEN_32;
		break;
	default:
		dev_err(component->dev, "%s: EXIT [-EINVAL]: bad frame size: "
			"%d\n", __func__, params_width(params));
		return -EINVAL;
	}

	ret = regmap_update_bits(zpcm512x->regmap, PCM512x_I2S_1,
				 PCM512x_ALEN, alen);
	if (ret != 0) {
		dev_err(component->dev, "%s: EXIT [%d]: failed to set frame "
			"size!\n", __func__, ret);
		return ret;
	}

	if ((zpcm512x->fmt & SND_SOC_DAIFMT_MASTER_MASK) ==
	    SND_SOC_DAIFMT_CBS_CFS) {
		ret = regmap_update_bits(zpcm512x->regmap, PCM512x_ERROR_DETECT,
					 PCM512x_DCAS, 0);
		if (ret != 0) {
			dev_err(component->dev, "%s: EXIT [%d]: failed to "
				"enable clock divider autoset!\n", __func__,
				ret);
			return ret;
		}

		goto skip_pll;
	}

	if (zpcm512x->pll_out) {
		ret = regmap_write(zpcm512x->regmap, PCM512x_FLEX_A, 0x11);
		if (ret != 0) {
			dev_err(component->dev, "%s: EXIT [%d]: failed to set "
				"FLEX_A!\n", __func__, ret);
			return ret;
		}

		ret = regmap_write(zpcm512x->regmap, PCM512x_FLEX_B, 0xff);
		if (ret != 0) {
			dev_err(component->dev, "%s: EXIT [%d]: failed to set "
				"FLEX_B!\n", __func__, ret);
			return ret;
		}

		ret = regmap_update_bits(zpcm512x->regmap, PCM512x_ERROR_DETECT,
					 PCM512x_IDFS | PCM512x_IDBK
					 | PCM512x_IDSK | PCM512x_IDCH
					 | PCM512x_IDCM | PCM512x_DCAS
					 | PCM512x_IPLK,
					 PCM512x_IDFS | PCM512x_IDBK
					 | PCM512x_IDSK | PCM512x_IDCH
					 | PCM512x_DCAS);
		if (ret != 0) {
			dev_err(component->dev, "%s: EXIT [%d]: failed to "
				"ignore auto-clock failures!\n", __func__, ret);
			return ret;
		}
	} else {
		ret = regmap_update_bits(zpcm512x->regmap, PCM512x_ERROR_DETECT,
					 PCM512x_IDFS | PCM512x_IDBK
					 | PCM512x_IDSK | PCM512x_IDCH
					 | PCM512x_IDCM | PCM512x_DCAS
					 | PCM512x_IPLK,
					 PCM512x_IDFS | PCM512x_IDBK
					 | PCM512x_IDSK | PCM512x_IDCH
					 | PCM512x_DCAS | PCM512x_IPLK);
		if (ret != 0) {
			dev_err(component->dev, "%s: EXIT [%d]: failed to "
				"ignore auto-clock failures!\n", __func__, ret);
			return ret;
		}

		ret = regmap_update_bits(zpcm512x->regmap, PCM512x_PLL_EN,
					 PCM512x_PLLE, 0);
		if (ret != 0) {
			dev_err(component->dev, "%s: EXIT [%d]: failed to "
				"disable pll!\n", __func__, ret);
			return ret;
		}
	}

	ret = zpcm512x_set_dividers(dai, params);
	if (ret != 0) {
		dev_err(component->dev, "%s: EXIT [%d]: failed to set "
			"dividers!\n", __func__, ret);
		return ret;
	}

	if (zpcm512x->pll_out) {
		ret = regmap_update_bits(zpcm512x->regmap, PCM512x_PLL_REF,
					 PCM512x_SREF, PCM512x_SREF_GPIO);
		if (ret != 0) {
			dev_err(component->dev, "%s: EXIT [%d]: failed to set "
				"gpio as pllref!\n", __func__, ret);
			return ret;
		}

		gpio = PCM512x_GREF_GPIO1 + zpcm512x->pll_in - 1;
		ret = regmap_update_bits(zpcm512x->regmap, PCM512x_GPIO_PLLIN,
					 PCM512x_GREF, gpio);
		if (ret != 0) {
			dev_err(component->dev, "%s: EXIT [%d]: failed to set "
				"gpio %d as pllin!\n", __func__, ret,
				zpcm512x->pll_in);
			return ret;
		}

		ret = regmap_update_bits(zpcm512x->regmap, PCM512x_PLL_EN,
					 PCM512x_PLLE, PCM512x_PLLE);
		if (ret != 0) {
			dev_err(component->dev, "%s: EXIT [%d]: failed to "
				"enable pll!\n", __func__, ret);
			return ret;
		}

		gpio = PCM512x_G1OE << (zpcm512x->pll_out - 1);
		ret = regmap_update_bits(zpcm512x->regmap, PCM512x_GPIO_EN,
					 gpio, gpio);
		if (ret != 0) {
			dev_err(component->dev, "%s: EXIT [%d]: failed to "
				"enable gpio %d!\n", __func__, ret,
				zpcm512x->pll_out);
			return ret;
		}

		gpio = PCM512x_GPIO_OUTPUT_1 + zpcm512x->pll_out - 1;
		ret = regmap_update_bits(zpcm512x->regmap, gpio,
					 PCM512x_GxSL, PCM512x_GxSL_PLLCK);
		if (ret != 0) {
			dev_err(component->dev, "%s: EXIT [%d]: failed to "
				"output pll on %d!\n", __func__, ret,
				zpcm512x->pll_out);
			return ret;
		}
	}

	ret = regmap_update_bits(zpcm512x->regmap, PCM512x_SYNCHRONIZE,
				 PCM512x_RQSY, PCM512x_RQSY_HALT);
	if (ret != 0) {
		dev_err(component->dev, "%s: EXIT [%d]: failed to halt "
			"clocks!\n", __func__, ret);
		return ret;
	}

	ret = regmap_update_bits(zpcm512x->regmap, PCM512x_SYNCHRONIZE,
				 PCM512x_RQSY, PCM512x_RQSY_RESUME);
	if (ret != 0) {
		dev_err(component->dev, "%s: EXIT [%d]: failed to resume "
			"clocks!\n", __func__, ret);
		return ret;
	}

skip_pll:
	dev_dbg(component->dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static int zpcm512x_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);
	int afmt;
	int offset = 0;
	int clock_output;
	int master_mode;
	int ret;

#ifdef DDEBUG
	dev_dbg(component->dev, "%s: ENTER: fmt=0x%x (MASTER=%s, FORMAT=%s, "
		"INV=%s, CLOCK=%s)\n", __func__, fmt,
		dd_utils_log_daifmt_master(fmt),
		dd_utils_log_daifmt_format(fmt),
		dd_utils_log_daifmt_inverse(fmt),
		dd_utils_log_daifmt_clock(fmt));
#else
	dev_dbg(component->dev, "%s: ENTER: fmt=0x%x\n", __func__, fmt);
#endif /* DDEBUG */

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		clock_output = 0;
		master_mode = 0;
#ifdef DDEBUG
		dev_dbg(component->dev, "%s: set BCLK_LRCLK_CFG=0\n", __func__);
		dev_dbg(component->dev, "%s: set MASTER_MODE=0\n", __func__);
#endif /* DDEBUG */
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		clock_output = PCM512x_BCKO | PCM512x_LRKO;
		master_mode = PCM512x_RLRK | PCM512x_RBCK;
#ifdef DDEBUG
		dev_dbg(component->dev, "%s: set BCLK_LRCLK_CFG=BCKO|LRKO\n",
			__func__);
		dev_dbg(component->dev, "%s: set MASTER_MODE=RBCK|RLRK\n",
			__func__);
#endif /* DDEBUG */
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		clock_output = PCM512x_BCKO;
		master_mode = PCM512x_RBCK;
#ifdef DDEBUG
		dev_dbg(component->dev, "%s: set BCLK_LRCLK_CFG=BCKO\n",
			__func__);
		dev_dbg(component->dev, "%s: set MASTER_MODE=RBCK\n", __func__);
#endif /* DDEBUG */
		break;
	default:
		dev_err(component->dev, "%s: EXIT [-EINVAL]: unsupported "
			"DAIFMT_MASTER 0x%x: returning [-EINVAL]\n", __func__,
			fmt & SND_SOC_DAIFMT_MASTER_MASK);
		return -EINVAL;
	}

	ret = regmap_update_bits(zpcm512x->regmap, PCM512x_BCLK_LRCLK_CFG,
				 PCM512x_BCKP | PCM512x_BCKO | PCM512x_LRKO,
				 clock_output);
	if (ret != 0) {
		dev_err(component->dev, "%s: EXIT [%d]: failed to enable "
			"clock output!\n", __func__, ret);
		return ret;
	}

	ret = regmap_update_bits(zpcm512x->regmap, PCM512x_MASTER_MODE,
				 PCM512x_RLRK | PCM512x_RBCK,
				 master_mode);
	if (ret != 0) {
		dev_err(component->dev, "%s: EXIT [%d]: failed to enable "
			"master mode!\n", __func__, ret);
		return ret;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		afmt = PCM512x_AFMT_I2S;
#ifdef DDEBUG
		dev_dbg(component->dev, "%s: set I2S_1=AFMT_I2S\n",
			__func__);
#endif /* DDEBUG */
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		afmt = PCM512x_AFMT_RTJ;
#ifdef DDEBUG
		dev_dbg(component->dev, "%s: set I2S_1=AFMT_RTJ\n",
			__func__);
#endif /* DDEBUG */
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		afmt = PCM512x_AFMT_LTJ;
#ifdef DDEBUG
		dev_dbg(component->dev, "%s: set I2S_1=AFMT_LTJ\n",
			__func__);
#endif /* DDEBUG */
		break;
	case SND_SOC_DAIFMT_DSP_A:
		offset = 1;
		fallthrough;
	case SND_SOC_DAIFMT_DSP_B:
		afmt = PCM512x_AFMT_DSP;
#ifdef DDEBUG
		dev_dbg(component->dev, "%s: set I2S_1=AFMT_DSP\n",
			__func__);
#endif /* DDEBUG */
		break;
	default:
		dev_err(component->dev, "%s: EXIT [-EINVAL]: unsupported "
			"DAIFMT_FORMAT 0x%x: returning [-EINVAL]\n",
			__func__, fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	ret = regmap_update_bits(zpcm512x->regmap, PCM512x_I2S_1,
				 PCM512x_AFMT, afmt);
	if (ret != 0) {
		dev_err(component->dev, "%s: EXIT [%d]: failed to set data "
			"format!\n", __func__, ret);
		return ret;
	}

#ifdef DDEBUG
	dev_dbg(component->dev, "%s: set I2S_2=%d (offset)\n", __func__,
		offset);
#endif /* DDEBUG */
	ret = regmap_update_bits(zpcm512x->regmap, PCM512x_I2S_2,
				 0xFF, offset);
	if (ret != 0) {
		dev_err(component->dev, "%s: EXIT [%d]: failed to set data "
			"offset!\n", __func__, ret);
		return ret;
	}

	zpcm512x->fmt = fmt;

	dev_dbg(component->dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static int zpcm512x_dai_set_bclk_ratio(struct snd_soc_dai *dai,
				       unsigned int ratio)
{
	struct snd_soc_component *component = dai->component;
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "%s: ENTER: ratio=%d\n", __func__, ratio);

	if (ratio > 256) {
		dev_err(component->dev, "%s: EXIT [-EINVAL]: ratio>256: "
			"returning [-EINVAL]\n", __func__);
		return -EINVAL;
	}

	zpcm512x->bclk_ratio = ratio;

	dev_dbg(component->dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static int zpcm512x_dai_mute_stream(struct snd_soc_dai *dai, int mute,
				    int direction)
{
	struct snd_soc_component *component = dai->component;
	struct zpcm512x_priv *zpcm512x =
				snd_soc_component_get_drvdata(component);
	int ret;
	unsigned int mute_det;
	unsigned int mute_enable = PCM512x_RQML | PCM512x_RQMR;
	char *mute_enable_log = "LEFT|RIGHT";
	int polling_timeout_us = 10000;

	dev_dbg(component->dev, "%s: ENTER: mute=%d, direction=%d\n", __func__,
		mute, direction);

	if (direction != SNDRV_PCM_STREAM_PLAYBACK) {
		dev_dbg(component->dev, "%s: EXIT [0]: noop - (direction != "
			"SNDRV_PCM_STREAM_PLAYBACK)\n", __func__);
		return 0;
	}

	mutex_lock(&zpcm512x->mutex);

	if (mute) {
		zpcm512x->mute |= 0x1;
#ifdef DDEBUG
		dev_dbg(component->dev, "%s: set PCM512x_MUTE=%s\n", __func__,
			mute_enable_log);
#endif /* DDEBUG */
		ret = regmap_update_bits(zpcm512x->regmap, PCM512x_MUTE,
					 mute_enable, mute_enable);
		if (ret != 0) {
			mutex_unlock(&zpcm512x->mutex);
			dev_err(component->dev, "%s: EXIT [%d]: failed setting "
				"PCM512x_MUTE=%s!\n", __func__, ret,
				mute_enable_log);
			return ret;
		}
#ifdef DDEBUG
		dev_dbg(component->dev, "%s: polling for ANALOG_MUTE_DET\n",
			__func__);
#endif /* DDEBUG */
		ret = regmap_read_poll_timeout(zpcm512x->regmap,
					       PCM512x_ANALOG_MUTE_DET,
					       mute_det,
					       (mute_det & 0x3) == 0,
					       200, polling_timeout_us);
		/* 
		 * Returns 0 on success and -ETIMEDOUT upon a timeout or the 
		 * regmap_read error return value in case of a error read.
		 */
		if (ret < 0) {
			if (ret == -ETIMEDOUT)
				dev_warn(component->dev, "%s: polling for "
					 "ANALOG_MUTE_DET returns "
					 "[-ETIMEDOUT]\n", __func__);
			else
				dev_warn(component->dev, "%s: polling for "
					 "ANALOG_MUTE_DET returns [%d]\n",
					 __func__, ret);
			ret = 0;
		}
		/* gpio mute */
		if (zpcm512x->mute_gpio && zpcm512x->auto_gpio_mute) {
#ifdef DDEBUG
			dev_dbg(component->dev, "%s: mute: "
				"gpiod_set_raw_value_cansleep(mute, 0)\n",
				__func__);
#endif /* DDEBUG */
			gpiod_set_raw_value_cansleep(zpcm512x->mute_gpio, 0);
		}
	} else {
		/* gpio unmute */
		if (zpcm512x->mute_gpio && zpcm512x->auto_gpio_mute) {
#ifdef DDEBUG
			dev_dbg(component->dev, "%s: unmute: "
				"gpiod_set_raw_value_cansleep(mute, 1)\n",
				__func__);
#endif /* DDEBUG */
			gpiod_set_raw_value_cansleep(zpcm512x->mute_gpio, 1);
		}

		zpcm512x->mute &= ~0x1;
		ret = zpcm512x_update_mute(component);
		if (ret != 0) {
			mutex_unlock(&zpcm512x->mutex);
			dev_err(component->dev, "%s: EXIT [%d]: failed to "
				"update digital mute!\n", __func__, ret);
			return ret;
		}
#ifdef DDEBUG
		dev_dbg(component->dev, "%s: polling for ANALOG_MUTE_DET\n",
			__func__);
#endif /* DEBUG */
		ret = regmap_read_poll_timeout(zpcm512x->regmap,
					       PCM512x_ANALOG_MUTE_DET,
					       mute_det,
			(mute_det & 0x3) == ((~zpcm512x->mute >> 1) & 0x3),
					       200, polling_timeout_us);
		/* 
		 * Returns 0 on success and -ETIMEDOUT upon a timeout or the 
		 * regmap_read error return value in case of a error read.
		 */
		if (ret < 0) {
			if (ret == -ETIMEDOUT)
				dev_warn(component->dev, "%s: polling for "
					 "ANALOG_MUTE_DET returns "
					 "[-ETIMEDOUT]\n", __func__);
			else
				dev_warn(component->dev, "%s: polling for "
					 "ANALOG_MUTE_DET returns [%d]\n",
					 __func__, ret);
			ret = 0;
		}
	}

	mutex_unlock(&zpcm512x->mutex);

	dev_dbg(component->dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static const struct snd_soc_dai_ops zpcm512x_dai_ops = {
	.startup         = zpcm512x_dai_startup,
	.hw_params       = zpcm512x_dai_hw_params,
	.set_fmt         = zpcm512x_dai_set_fmt,
	.mute_stream     = zpcm512x_dai_mute_stream,
 	.set_bclk_ratio  = zpcm512x_dai_set_bclk_ratio,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
 	.no_capture_mute = 1,
#endif
};

static struct snd_soc_dai_driver zpcm512x_dai_drv = {
	.name     = "zpcm512x-hifi",
	.playback = {
		.stream_name  = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates        = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min     = 8000,
		.rate_max     = 384000,
		.formats      = SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S32_LE
	},
	.ops      = &zpcm512x_dai_ops,
};

static const struct snd_soc_component_driver zpcm512x_comp_drv = {
	.set_bias_level        = zpcm512x_set_bias_level,
	.controls              = zpcm512x_controls,
	.num_controls          = ARRAY_SIZE(zpcm512x_controls),
	.dapm_widgets          = zpcm512x_dapm_widgets,
	.num_dapm_widgets      = ARRAY_SIZE(zpcm512x_dapm_widgets),
	.dapm_routes           = zpcm512x_dapm_routes,
	.num_dapm_routes       = ARRAY_SIZE(zpcm512x_dapm_routes),
	.use_pmdown_time       = 1,
	.endianness            = 1,
	.non_legacy_dai_naming = 1,
};

static const struct regmap_range_cfg zpcm512x_ranges = {
	.name          = "Pages",
	.range_min     = PCM512x_VIRT_BASE,
	.range_max     = PCM512x_MAX_REGISTER,
	.selector_reg  = PCM512x_PAGE,
	.selector_mask = 0xff,
	.window_start  = 0,
	.window_len    = 0x100,
};

const struct regmap_config zpcm512x_regmap = {
	.reg_bits         = 8,
	.val_bits         = 8,

	.readable_reg     = zpcm512x_readable_reg,
	.volatile_reg     = zpcm512x_volatile_reg,

	.ranges           = &zpcm512x_ranges,
	.num_ranges       = 1,

	.max_register     = PCM512x_MAX_REGISTER,
	.reg_defaults     = zpcm512x_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(zpcm512x_reg_defaults),
	.cache_type       = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_GPL(zpcm512x_regmap);

int zpcm512x_probe(struct device *dev, struct regmap *regmap)
{
	struct zpcm512x_priv *zpcm512x;
	int i, ret;

	dev_dbg(dev, "%s: ENTER\n", __func__);

#ifdef DDEBUG
	dev_dbg(dev, "%s: allocate memory for private data\n", __func__);
#endif /* DDEBUG */
	zpcm512x = devm_kzalloc(dev, sizeof(struct zpcm512x_priv), GFP_KERNEL);
	if (!zpcm512x) {
		dev_err(dev, "%s: EXIT [-ENOMEM]: failed to allocate memory "
			"for private data!\n", __func__);
		return -ENOMEM;
	}

	mutex_init(&zpcm512x->mutex);
	
	dev_set_drvdata(dev, zpcm512x);
	zpcm512x->regmap = regmap;

#ifdef CONFIG_OF
	/*
	 * optional mute gpio
	 * NB. gpio default is active low
	 */
#ifdef DDEBUG
	dev_dbg(dev, "%s: devm_gpiod_get_optional(mute, "
		"PCM512X_GPIOD_OUT_LOW)\n", __func__);
#endif /* DDEBUG */
	zpcm512x->mute_gpio = devm_gpiod_get_optional(dev, "mute",
						      PCM512X_GPIOD_OUT_LOW);
	if (IS_ERR(zpcm512x->mute_gpio)) {
		ret = PTR_ERR(zpcm512x->mute_gpio);
		if (ret == -EPROBE_DEFER)
			dev_info(dev, "%s: devm_gpiod_get_optional(mute) "
				 "returns: [-EPROBE_DEFER]\n", __func__);
		else
			dev_err(dev, "%s: devm_gpiod_get_optional(mute) "
				"failed: [%d]\n", __func__, ret);
		return ret;
	}
#ifdef DDEBUG
	if (zpcm512x->mute_gpio)
		dev_dbg(dev, "%s: obtained reference to optional mute gpio\n",
			__func__);
	else
		dev_dbg(dev, "%s: did not obtain reference to optional mute "
			"gpio\n", __func__); 
#endif /* DDEBUG */
#endif /* CONFIG_OF */

	for (i = 0; i < ARRAY_SIZE(zpcm512x->supplies); i++)
		zpcm512x->supplies[i].supply = zpcm512x_supply_names[i];

#ifdef DDEBUG
	dev_dbg(dev, "%s: devm_regulator_bulk_get()\n", __func__);
#endif /* DDEBUG */
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(zpcm512x->supplies),
				      zpcm512x->supplies);
	if (ret != 0) {
		dev_err(dev, "%s: EXIT [%d]: failed to get supplies!\n",
			__func__, ret);
		return ret;
	}

	zpcm512x->supply_nb[0].notifier_call = zpcm512x_regulator_event_0;
	zpcm512x->supply_nb[1].notifier_call = zpcm512x_regulator_event_1;
	zpcm512x->supply_nb[2].notifier_call = zpcm512x_regulator_event_2;

	for (i = 0; i < ARRAY_SIZE(zpcm512x->supplies); i++) {
		ret = devm_regulator_register_notifier(
						zpcm512x->supplies[i].consumer,
						&zpcm512x->supply_nb[i]);
		if (ret != 0) {
			dev_err(dev, "%s: failed to register regulator "
				"notifier: %d\n", __func__, ret);
		}
	}

#ifdef DDEBUG
	dev_dbg(dev, "%s: enabling supplies\n", __func__);
#endif /* DDEBUG */
	ret = regulator_bulk_enable(ARRAY_SIZE(zpcm512x->supplies),
				    zpcm512x->supplies);
	if (ret != 0) {
		dev_err(dev, "%s: EXIT [%d]: failed to enable supplies!\n",
			__func__, ret);
		return ret;
	}

	/* Reset the device, verifying I/O in the process for I2C */
#ifdef DDEBUG
        dev_dbg(dev, "%s: reset device\n", __func__);
#endif /* DDEBUG */
	ret = regmap_write(regmap, PCM512x_RESET,
			   PCM512x_RSTM | PCM512x_RSTR);
	if (ret != 0) {
		dev_err(dev, "%s: failed to reset device: %d\n", __func__, ret);
		goto err;
	}

	ret = regmap_write(regmap, PCM512x_RESET, 0);
	if (ret != 0) {
		dev_err(dev, "%s: failed to reset device: %d\n", __func__, ret);
		goto err;
	}

	/*
	 * NB. Make sure the DAC is muted after the reset, because we might
	 *     disable power management later
	 */
#ifdef DDEBUG
	dev_dbg(dev, "%s: mute device\n", __func__);
#endif /* DDEBUG */
	ret = regmap_update_bits(regmap, PCM512x_MUTE, 
				 PCM512x_RQML | PCM512x_RQMR,
				 PCM512x_RQML | PCM512x_RQMR);
	if (ret != 0) {
		dev_err(dev, "%s: failed to mute device: %d\n", __func__, ret);
		goto err;
	}

#ifdef DDEBUG
	dev_dbg(dev, "%s: devm_clk_get(NULL)\n", __func__);
#endif /* DDEBUG */
	zpcm512x->sclk = devm_clk_get(dev, NULL);
	if (PTR_ERR(zpcm512x->sclk) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		dev_info(dev, "%s: devm_clk_get(NULL) returns: "
			 "[-EPROBE_DEFER]\n", __func__);
		goto err;
	}
	if (!IS_ERR(zpcm512x->sclk)) {
#ifdef DDEBUG
		dev_dbg(dev, "%s: clk_prepare_enable(sclk)\n", __func__);
#endif /* DDEBUG */
		ret = clk_prepare_enable(zpcm512x->sclk);
		if (ret != 0) {
			dev_err(dev, "%s: clk_prepare_enable(sclk) failed: "
				"%d\n", __func__, ret);
			goto err;
		}
	}

#ifdef CONFIG_OF
	if (dev->of_node) {
		const struct device_node *np = dev->of_node;
		u32 val;

		if (of_property_read_u32(np, "pll-in", &val) >= 0) {
			if (val > 6) {
				dev_err(dev, "%s: invalid pll-in\n", __func__);
				ret = -EINVAL;
				goto err_clk;
			}
			zpcm512x->pll_in = val;
		}

		if (of_property_read_u32(np, "pll-out", &val) >= 0) {
			if (val > 6) {
				dev_err(dev, "%s: invalid pll-out\n", __func__);
				ret = -EINVAL;
				goto err_clk;
			}
			zpcm512x->pll_out = val;
		}

		if (!zpcm512x->pll_in != !zpcm512x->pll_out) {
			dev_err(dev, "%s: error: both pll-in and pll-out, or "
				"none\n", __func__);
			ret = -EINVAL;
			goto err_clk;
		}
		if (zpcm512x->pll_in && zpcm512x->pll_in == zpcm512x->pll_out) {
			dev_err(dev, "%s: error: pll-in == pll-out\n",
				__func__);
			ret = -EINVAL;
			goto err_clk;
		}
		/* auto_gpio_mute */
		if (zpcm512x->mute_gpio) {
			zpcm512x->auto_gpio_mute = of_property_read_bool(np,
						     "pcm512x,auto-gpio-mute");
		}
		zpcm512x->disable_pwrdown = of_property_read_bool(np,
						"pcm512x,disable-pwrdown");
		zpcm512x->disable_standby = of_property_read_bool(np,
						"pcm512x,disable-standby");
	}
#endif /* CONFIG_OF */

	if (!zpcm512x->disable_standby) {
		/* Default to standby mode */
		ret = regmap_update_bits(zpcm512x->regmap, PCM512x_POWER,
					 PCM512x_RQST, PCM512x_RQST);
		if (ret != 0) {
			dev_err(dev, "%s: failed to request standby: %d\n",
				__func__, ret);
			goto err_clk;
		}
	} else
		dev_info(dev, "%s: RQST standby is disabled\n", __func__);

	if (!zpcm512x->disable_pwrdown) {
#ifdef DDEBUG
		dev_dbg(dev, "%s: pm_runtime_set_active()\n", __func__);
#endif
		pm_runtime_set_active(dev);
#ifdef DDEBUG
		dev_dbg(dev, "%s: pm_runtime_enable()\n", __func__);
#endif
		pm_runtime_enable(dev);
#ifdef DDEBUG
		dev_dbg(dev, "%s: pm_runtime_idle()\n", __func__);
#endif
		pm_runtime_idle(dev);
	} else
		dev_info(dev, "%s: RQPD powerdown is disabled\n", __func__);

	/*
	 * !auto_gpio_mute: one-time gpio unmute
	 */
	if (zpcm512x->mute_gpio && !zpcm512x->auto_gpio_mute) {
#ifdef DDEBUG
		dev_dbg(dev, "%s: !auto_gpio_mute: unmute: "
			"gpiod_set_raw_value_cansleep(mute, 1)\n",
			__func__);
#endif /* DDEBUG */
		gpiod_set_raw_value_cansleep(zpcm512x->mute_gpio, 1);
	}

#ifdef DDEBUG
        dev_dbg(dev, "%s: devm_snd_soc_register_component()\n", __func__);
#endif /* DDEBUG */
	ret = devm_snd_soc_register_component(dev, &zpcm512x_comp_drv,
					      &zpcm512x_dai_drv, 1);
	if (ret != 0) {
		dev_err(dev, "%s: failed to register CODEC: %d\n", __func__,
			ret);
		goto err_gpio;
	}

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;

err_gpio:
	/* gpio mute */
	if (zpcm512x->mute_gpio && !zpcm512x->auto_gpio_mute) {
#ifdef DDEBUG
		dev_dbg(dev, "%s: mute: "
			"gpiod_set_raw_value_cansleep(mute, 0)\n", __func__);
#endif /* DDEBUG */
		gpiod_set_raw_value_cansleep(zpcm512x->mute_gpio, 0);
	}
//err_pm:
	if (!zpcm512x->disable_pwrdown) { 
#ifdef DDEBUG
		dev_dbg(dev, "%s: pm_runtime_disable()\n", __func__);
#endif
		pm_runtime_disable(dev);
	}
err_clk:
	if (!IS_ERR(zpcm512x->sclk)) {
#ifdef DDEBUG
		dev_dbg(dev, "%s: clk_disable_unprepare(sclk)\n", __func__);
#endif /* DDEBUG */
		clk_disable_unprepare(zpcm512x->sclk);
	}
err:
	regulator_bulk_disable(ARRAY_SIZE(zpcm512x->supplies),
			       zpcm512x->supplies);

	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "%s: EXIT [%d]\n", __func__, ret);
		else
			dev_info(dev, "%s: EXIT [-EPROBE_DEFER]\n", __func__);
	} else
		dev_dbg(dev, "%s: EXIT [%d]\n", __func__, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(zpcm512x_probe);

void zpcm512x_remove(struct device *dev)
{
	struct zpcm512x_priv *zpcm512x = dev_get_drvdata(dev);

	dev_dbg(dev, "%s: ENTER\n", __func__);

	/* gpio mute */
	if (zpcm512x->mute_gpio) {
#ifdef DDEBUG
		dev_dbg(dev, "%s: mute: "
			"gpiod_set_raw_value_cansleep(mute, 0)\n", __func__);
#endif /* DDEBUG */
		gpiod_set_raw_value_cansleep(zpcm512x->mute_gpio, 0);
	}

	if (!zpcm512x->disable_pwrdown) {
#ifdef DDEBUG
		dev_dbg(dev, "%s: pm_runtime_disable()\n", __func__);
#endif
		pm_runtime_disable(dev);
	}

	if (!IS_ERR(zpcm512x->sclk)) {
#ifdef DDEBUG
		dev_dbg(dev, "%s: clk_disable_unprepare(sclk)\n", __func__);
#endif /* DDEBUG */
		clk_disable_unprepare(zpcm512x->sclk);
	}

	regulator_bulk_disable(ARRAY_SIZE(zpcm512x->supplies),
			       zpcm512x->supplies);

	dev_dbg(dev, "%s: EXIT\n", __func__);
}
EXPORT_SYMBOL_GPL(zpcm512x_remove);

#ifdef CONFIG_PM
static int zpcm512x_suspend(struct device *dev)
{
	struct zpcm512x_priv *zpcm512x = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "%s: ENTER\n", __func__);
	/* gpio mute */
	if (zpcm512x->mute_gpio && !zpcm512x->auto_gpio_mute) {
#ifdef DDEBUG
		dev_dbg(dev, "%s: mute: "
			"gpiod_set_raw_value_cansleep(mute, 0)\n", __func__);
#endif /* DDEBUG */
		gpiod_set_raw_value_cansleep(zpcm512x->mute_gpio, 0);
	}
#ifdef DDEBUG
	dev_dbg(dev, "%s: set RQPD to power down mode\n", __func__);
#endif /* DDEBUG */
	ret = regmap_update_bits(zpcm512x->regmap, PCM512x_POWER,
				 PCM512x_RQPD, PCM512x_RQPD);
	if (ret != 0) {
		dev_err(dev, "%s: EXIT [%d]: failed setting RQPD to power down "
			"mode!\n", __func__, ret);
		return ret;
	}
#ifdef DDEBUG
	dev_dbg(dev, "%s: disabling supplies\n", __func__);
#endif /* DDEBUG */
	ret = regulator_bulk_disable(ARRAY_SIZE(zpcm512x->supplies),
				     zpcm512x->supplies);
	if (ret != 0) {
		dev_err(dev, "%s: EXIT [%d]: failed to disable supplies!\n",
			__func__, ret);
		return ret;
	}

	if (!IS_ERR(zpcm512x->sclk)) {
#ifdef DDEBUG
	dev_dbg(dev, "%s: clk_disable_unprepare(sclk)\n", __func__);
#endif /* DDEBUG */
		clk_disable_unprepare(zpcm512x->sclk);
	}

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static int zpcm512x_resume(struct device *dev)
{
	struct zpcm512x_priv *zpcm512x = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "%s: ENTER\n", __func__);

	if (!IS_ERR(zpcm512x->sclk)) {
#ifdef DDEBUG
		dev_dbg(dev, "%s: clk_prepare_enable(sclk)\n", __func__);
#endif /* DDEBUG */
		ret = clk_prepare_enable(zpcm512x->sclk);
		if (ret != 0) {
			dev_err(dev, "%s: EXIT [%d]: failed to enable SCLK!\n",
				__func__, ret);
			return ret;
		}
	}
#ifdef DDEBUG
	dev_dbg(dev, "%s: enabling supplies\n", __func__);
#endif /* DDEBUG */
	ret = regulator_bulk_enable(ARRAY_SIZE(zpcm512x->supplies),
				    zpcm512x->supplies);
	if (ret != 0) {
		dev_err(dev, "%s: EXIT [%d]: failed to enable supplies!\n",
			__func__, ret);
		return ret;
	}
#ifdef DDEBUG
	dev_dbg(dev, "%s: sync regmap cache\n", __func__);
#endif /* DDEBUG */
	regcache_cache_only(zpcm512x->regmap, false);
	ret = regcache_sync(zpcm512x->regmap);
	if (ret != 0) {
		dev_err(dev, "%s: EXIT [%d]: failed to sync regmap cache!\n",
			__func__, ret);
		return ret;
	}
#ifdef DDEBUG
	dev_dbg(dev, "%s: set RQPD to normal operation\n", __func__);
#endif /* DDEBUG */
	ret = regmap_update_bits(zpcm512x->regmap, PCM512x_POWER,
				 PCM512x_RQPD, 0);
	if (ret != 0) {
		dev_err(dev, "%s: EXIT [%d]: failed setting RQPD to normal "
			"operation!\n", __func__, ret);
		return ret;
	}
	/* gpio unmute */
	if (zpcm512x->mute_gpio && !zpcm512x->auto_gpio_mute) {
#ifdef DDEBUG
		dev_dbg(dev, "%s: unmute: "
			"gpiod_set_raw_value_cansleep(mute, 1)\n", __func__);
#endif /* DDEBUG */
		gpiod_set_raw_value_cansleep(zpcm512x->mute_gpio, 1);
	}

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}
#endif

const struct dev_pm_ops zpcm512x_pm_ops = {
	SET_RUNTIME_PM_OPS(zpcm512x_suspend, zpcm512x_resume, NULL)
};
EXPORT_SYMBOL_GPL(zpcm512x_pm_ops);

MODULE_DESCRIPTION("ALTernative ASoC PCM512x codec driver");
MODULE_AUTHOR("Mark Brown <broonie@kernel.org>");
MODULE_AUTHOR("Clive Messer <clive.messer@digitaldreamtime.co.uk>");
MODULE_LICENSE("GPL v2");
