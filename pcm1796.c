// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PCM1796 ASoC codec driver
 *
 * Author: Michael Trimarchi <michael@amarulasolutions.com>
 *         Copyright (c) Amarula Solutions B.V. 2013
 *
 * Author: Clive Messer <clive.messer@digitaldreamtime.co.uk>
 *         Copyright (c) Digital Dreamtime Ltd 2020-2021
 */

#include <linux/gpio/consumer.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/clk.h>
#include <linux/of.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "pcm1796.h"
#include "dd-utils.h"

#ifdef PCM1796_GPIO_ACTIVE_HIGH
#define PCM1796_GPIOD_OUT_LOW	GPIOD_OUT_LOW
#else /* Pi gpio default is active_low, so need to set logical high */
#define PCM1796_GPIOD_OUT_LOW	GPIOD_OUT_HIGH
#endif /* PCM1796_GPIO_ACTIVE_HIGH */

struct pcm1796_drvdata {
	struct mutex mutex;
	unsigned int format;
	unsigned int bclk_ratio;
	unsigned int rate;
	struct clk *sclk;
	unsigned long sysclk;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *mute_gpio;
	bool auto_gpio_mute;
};

static const struct reg_default pcm1796_reg_defaults[] = {
	{ PCM1796_REG16, 0xFF },
	{ PCM1796_REG17, 0xFF },
	{ PCM1796_REG18, 0x50 },
	{ PCM1796_REG19, 0x00 },
	{ PCM1796_REG20, 0x00 },
	{ PCM1796_REG21, 0x01 },
	{ PCM1796_REG22, 0x00 }, /* READ ONLY */
	{ PCM1796_REG23, 0x00 }, /* READ ONLY */
};

static bool pcm1796_accessible_reg(struct device *dev, unsigned int reg)
{
	bool result;
#ifdef DDDEBUG
	dev_dbg(dev, "%s: ENTER: reg=%u\n", __func__, reg);
#endif /* DDDEBUG */
	result = (reg >= PCM1796_REG16 && reg <= PCM1796_REG23);
#ifdef DDDEBUG
	dev_dbg(dev, "%s: EXIT [%s]\n", __func__, (result ? "true" : "false"));
#endif /* DDDEBUG */
	return result;
}

static bool pcm1796_writeable_reg(struct device *dev, unsigned int reg)
{
	bool result;
#ifdef DDDEBUG
	dev_dbg(dev, "%s: ENTER: reg=%u\n", __func__, reg);
#endif /* DDDEBUG */
	result = (reg >= PCM1796_REG16 && reg <= PCM1796_REG21);
#ifdef DDDEBUG
	dev_dbg(dev, "%s: EXIT [%s]\n", __func__, (result ? "true" : "false"));
#endif /* DDDEBUG */
	return result;
}

static bool pcm1796_volatile_reg(struct device *dev, unsigned int reg)
{
	bool result;
#ifdef DDDEBUG
	dev_dbg(dev, "%s: ENTER: reg=%u\n", __func__, reg);
#endif /* DDDEBUG */
	switch(reg) {
	case PCM1796_REG22:
	case PCM1796_REG23:
		result = true;
		break;
	default:
		result = false;
		break;
	}
#ifdef DDDEBUG
	dev_dbg(dev, "%s: EXIT [%s]\n", __func__, (result ? "true" : "false"));
#endif /* DDDEBUG */
	return result;
}

static int pcm1796_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct pcm1796_drvdata *data = snd_soc_component_get_drvdata(component);
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
	data->format = fmt;

	dev_dbg(component->dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static int pcm1796_dai_set_bclk_ratio(struct snd_soc_dai *dai,
				      unsigned int ratio)
{
	struct snd_soc_component *component = dai->component;
	struct pcm1796_drvdata *data = snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "%s: ENTER: ratio=%u\n", __func__, ratio);

	data->bclk_ratio = ratio;

	dev_dbg(component->dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static int pcm1796_dai_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				  unsigned int freq, int dir)
{
	int ret;
	struct snd_soc_component *component = dai->component;
	struct pcm1796_drvdata *data = snd_soc_component_get_drvdata(component);
	struct device *dev = component->dev;

	dev_dbg(dev, "%s: ENTER: clk_id=%d, freq=%u\n", __func__, clk_id, freq);

	if (clk_id != PCM1796_SYSCLK_ID) {
		dev_err(dev, "%s: EXIT [-EINVAL]: (clk_id=%d != "
			"PCM1796_SYSCLK_ID=%d): returning [-EINVAL]\n",
			__func__, clk_id, PCM1796_SYSCLK_ID);
		return -EINVAL;
	}

	if (freq > PCM1796_MAX_SYSCLK) {
		dev_err(dev, "%s: EXIT [-EINVAL]: (freq=%u > "
			"PCM1796_MAX_SYSCLK=%u): returning [-EINVAL]\n",
			__func__, freq, PCM1796_MAX_SYSCLK);
		return -EINVAL;
	}

	/*
	 * Some sound card sets 0 Hz as reset,
	 * but it is impossible to set. Ignore it here
	 */
	if (freq == 0) {
		dev_dbg(dev, "%s: EXIT [0]: noop - ignoring because freq=0",
			__func__);
        	return 0;
	}

#ifdef DDEBUG
	dev_dbg(dev, "%s: clk_set_rate(sclk, %u)\n", __func__, freq);
#endif /* DDEBUG */
	ret = clk_set_rate(data->sclk, freq);
	if (ret < 0) {
		dev_err(dev, "%s: EXIT [%d]: clk_set_rate(sclk, %u) failed!\n",
			__func__, ret, freq);
		return ret;
	}

	data->sysclk = freq;

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static int pcm1796_gpio_mute_enable(struct snd_soc_component *component,
				    bool enable)
{
	struct pcm1796_drvdata *data = snd_soc_component_get_drvdata(component);
	struct device *dev = component->dev;
	int gpio_enable = (enable ? 0 : 1);

	dev_dbg(dev, "%s: ENTER: enable=%s\n", __func__, (enable ? "true"
								 : "false"));
	if (data->mute_gpio) {
#ifdef DDEBUG
		dev_dbg(dev, "%s: gpiod_set_raw_value_cansleep(mute, %d)\n",
			__func__, gpio_enable);
#endif /* DDEBUG */
		gpiod_set_raw_value_cansleep(data->mute_gpio, gpio_enable);
	}
#ifdef DDEBUG
	  else
		dev_dbg(dev, "%s: mute_gpio == NULL: not setting gpio!\n",
			__func__);
#endif /* DDEBUG */

	dev_dbg(dev, "%s: EXIT[0]\n", __func__);
	return 0;
}

static int pcm1796_output_enable(struct snd_soc_component *component,
				 bool enable)
{
	int ret = 0;
	char *enable_log = (enable ? "REG19_OPE_ENABLE" : "REG19_OPE_DISABLE");
	struct device *dev = component->dev;

	dev_dbg(dev, "%s: ENTER: enable=%s\n", __func__, (enable ? "true"
								 : "false"));

	dev_dbg(dev, "%s: set %s\n", __func__, enable_log);
	ret = snd_soc_component_update_bits(component, PCM1796_REG19_OPE,
					    PCM1796_REG19_OPE_MASK,
					  (enable ? PCM1796_REG19_OPE_ENABLE
					    	  : PCM1796_REG19_OPE_DISABLE));
	if (ret) {
		if (ret < 0) {
			dev_err(dev, "%s: EXIT [%d]: failed to set %s!\n",
				__func__, ret, enable_log);
			return ret;
		}
#ifdef DDEBUG
		  else
			dev_dbg(dev, "%s: set %s returns: [%d]\n", __func__,
				enable_log, ret);
#endif /* DDEBUG */
	}

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static int pcm1796_mute_stream(struct snd_soc_component *component, int mute)
{
	int ret = 0;
	char *mute_log = (mute ? "REG18_MUTE_ENABLE" : "REG18_MUTE_DISABLE");
	struct pcm1796_drvdata *data = snd_soc_component_get_drvdata(component);
	struct device *dev = component->dev;
	
	dev_dbg(dev, "%s: ENTER: mute=%d\n", __func__, mute);
	
	mutex_lock(&data->mutex);

	/*
	 * unmute - enable output
	 */
	if (!mute) {
		if (data->auto_gpio_mute)
			pcm1796_gpio_mute_enable(component, false);
		pcm1796_output_enable(component, true);
	}

	dev_dbg(dev, "%s: set %s\n", __func__, mute_log);
	ret = snd_soc_component_update_bits(component, PCM1796_REG18_MUTE,
					    PCM1796_REG18_MUTE_MASK, !!mute);
	if (ret) {
		if (ret < 0) {
			dev_err(dev, "%s: error setting %s: [%d]\n", __func__,
				 mute_log, ret);
		}
#ifdef DDEBUG
		  else
			dev_dbg(dev, "%s: set %s returns: [%d]\n", __func__,
				mute_log, ret);
#endif /* DDEBUG */
	}

	/*
	 * mute - disable output
	 */
	if (mute) {
		pcm1796_output_enable(component, false);
		if (data->auto_gpio_mute)
			pcm1796_gpio_mute_enable(component, true);
	}

	mutex_unlock(&data->mutex);

	if (ret < 0) {
		dev_err(dev, "%s: EXIT [%d]\n", __func__, ret);
		return ret;
	}

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static int pcm1796_dai_mute_stream(struct snd_soc_dai *dai, int mute,
				   int direction)
{
	int ret = 0;
	struct snd_soc_component *component = dai->component;
	struct device *dev = component->dev;

	dev_dbg(dev, "%s: ENTER: mute=%d, direction=%d\n", __func__, mute,
		direction);	

	if (direction != SNDRV_PCM_STREAM_PLAYBACK) {
		dev_dbg(dev, "%s: EXIT [0]: noop - (direction != "
			"SNDRV_PCM_STREAM_PLAYBACK)\n", __func__);
		return 0;
	}

	ret = pcm1796_mute_stream(component, mute);
	if (ret < 0) {
		dev_err(dev, "%s: EXIT [%d]\n", __func__, ret);
		return ret;
	}

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static int pcm1796_dai_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	int ret, fmt_val = 0;
	struct snd_soc_component *component = dai->component;
	struct pcm1796_drvdata *data = snd_soc_component_get_drvdata(component);
	struct device *dev = component->dev;
	char *fmt_log;

	snd_pcm_format_t format = params_format(params);
	unsigned int rate = params_rate(params);

	dev_dbg(dev, "%s: ENTER: frequency=%u, format=%s, sample_bits=%u, "
		"physical_bits=%u, channels=%u\n", __func__, rate,
		snd_pcm_format_name(format), snd_pcm_format_width(format),
		snd_pcm_format_physical_width(format), params_channels(params));

	data->rate = rate;

	switch (data->format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		switch (params_width(params)) {
		case 24:
		case 32:
			fmt_val = PCM1796_REG18_FMT_RJ24;
			fmt_log = "REG18_FMT_RJ24";
			break;
		case 16:
			fmt_val = PCM1796_REG18_FMT_RJ16;
			fmt_log = "REG18_FMT_RJ16";
			break;
		default:
			dev_err(dev, "%s: EXIT [-EINVAL]: unsupported bit "
				"length %d in RIGHTJ mode: returning "
				"[-EINVAL]\n", __func__, params_width(params));
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_I2S:
		switch (params_width(params)) {
		case 24:
		case 32:
			fmt_val = PCM1796_REG18_FMT_I2S24;
			fmt_log = "REG18_FMT_I2S24";
			break;
		case 16:
			fmt_val = PCM1796_REG18_FMT_I2S16;
			fmt_log = "REG18_FMT_I2S16";
			break;
		default:
			dev_err(dev, "%s: EXIT [-EINVAL]: unsupported bit "
				"length %d in I2S mode: returning [-EINVAL]\n",
				__func__, params_width(params));
			return -EINVAL;
		}
		break;
	default:
		dev_err(dev, "%s: EXIT [-EINVAL]: unsupported DAIFMT_FORMAT "
			"0x%x: returning [-EINVAL]\n", __func__,
			data->format & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	dev_dbg(dev, "%s: set %s\n", __func__, fmt_log);
	ret = snd_soc_component_update_bits(component, PCM1796_REG18_FMT,
			PCM1796_REG18_FMT_MASK | PCM1796_REG18_ATLD_MASK,
			fmt_val | PCM1796_REG18_ATLD_ENABLE);
	if (ret) {
		if (ret < 0) {
			dev_err(dev, "%s: EXIT [%d]: failed to set %s!\n",
				__func__, ret, fmt_log);
			return ret;
		}
#ifdef DDEBUG
	  	  else
			dev_dbg(dev, "%s: set %s returns: [%d]\n",
				__func__, fmt_log, ret);
#endif /* DDEBUG */
	}

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}
#ifdef PCM1796_MUTE_SWITCH
static int pcm1796_digital_playback_switch_get(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	int mute;
	struct snd_soc_component *component =
					snd_soc_kcontrol_component(kcontrol);
	struct pcm1796_drvdata *data = snd_soc_component_get_drvdata(component);
	struct device *dev = component->dev;

	mutex_lock(&data->mutex);
#ifdef DEBUG
	dev_dbg(dev, "%s: ENTER\n", __func__);
#endif /* DEBUG */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
	mute = snd_soc_component_read(component, PCM1796_REG18_MUTE);
#else
	mute = snd_soc_component_read32(component, PCM1796_REG18_MUTE);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0) */
	if (mute < 0) {
		dev_err(dev, "%s: EXIT [%d]: error reading REG18_MUTE!\n",
			__func__, mute);
		mutex_unlock(&data->mutex);
		return mute;
	}

	mute = !(mute & PCM1796_REG18_MUTE_MASK);
#ifdef DDEBUG
	dev_dbg(dev, "%s: populate ucontrol value=%d", __func__, mute);
#endif /* DDEBUG */
	ucontrol->value.integer.value[0] = mute;
#ifdef DEBUG
	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
#endif /* DEBUG */
	mutex_unlock(&data->mutex);
	return 0;
}

static int pcm1796_digital_playback_switch_put(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0, mute = !ucontrol->value.integer.value[0];
	struct snd_soc_component *component =
					snd_soc_kcontrol_component(kcontrol);
	struct device *dev = component->dev;
#ifdef DEBUG
	dev_dbg(dev, "%s: ENTER\n", __func__);
#endif /* DEBUG */
	ret = pcm1796_mute_stream(component, mute);
	if (ret < 0) {
		dev_err(dev, "%s: EXIT [%d]: pcm1796_mute_stream(%s, %d) "
			"failed!\n", __func__, ret, component->name, mute);
		return ret;
	}	
#ifdef DEBUG
	dev_dbg(dev, "%s: EXIT [%d]\n", __func__, ret);
#endif /* DEBUG */
	return ret;
}
#endif /* PCM1796_MUTE_SWITCH */
/* Output Phase Reversal */
static const char * const pcm1796_channel_polarity_texts[] = {
	"Normal", "Invert",
};
static SOC_ENUM_SINGLE_DECL(pcm1796_polarity_enum, /* name */
			    PCM1796_REG19_REV, /* xreg */
			    PCM1796_REG19_REV_SHIFT, /* xshift */
			    pcm1796_channel_polarity_texts /* xtexts */ );

/* Digital Filter Roll-off Control */
static const char * const pcm1796_filter_shape_texts[] = {
	"Sharp Roll-Off",
	"Slow Roll-Off",
};
static SOC_ENUM_SINGLE_DECL(pcm1796_filter_shape_enum, /* name */
			    PCM1796_REG19_FLT, /* xreg */
			    PCM1796_REG19_FLT_SHIFT, /* xshift */
			    pcm1796_filter_shape_texts /* xtexts */ );

/* De-emphasis select */
static const char * const pcm1796_deemph_select_texts[] = {
	"Disabled", "48kHz", "44.1kHz", "32kHz",
};
static SOC_ENUM_SINGLE_DECL(pcm1796_deemph_select_enum,
			    PCM1796_REG18_DMF,
			    PCM1796_REG18_DMF_SHIFT,
			    pcm1796_deemph_select_texts);

/* Attenuation Rate Select */
static const char * pcm1796_atten_rate_select_texts[] = {
	"LRCK", "LRCK/2", "LRCK/4", "LRCK/8",
};
static SOC_ENUM_SINGLE_DECL(pcm1796_atten_rate_select_enum,
			    PCM1796_REG19_ATS,
			    PCM1796_REG19_ATS_SHIFT,
			    pcm1796_atten_rate_select_texts);

/* Infinite Zero Detect Mute Control */
static const char * pcm1796_inf_zero_detect_select_texts[] = {
	"Disable", "Enable",
};
static SOC_ENUM_SINGLE_DECL(pcm1796_inf_zero_detect_enum,
			    PCM1796_REG19_INZD,
			    PCM1796_REG19_INZD_SHIFT,
			    pcm1796_inf_zero_detect_select_texts);

/* Volume Control */
static const DECLARE_TLV_DB_SCALE(pcm1796_dac_tlv, -12000, 50, 1);

static const struct snd_kcontrol_new pcm1796_controls[] = {
	/* Volume Control */
	SOC_DOUBLE_R_RANGE_TLV("Digital Playback Volume", /* xname */
			       PCM1796_REG16_ATL, /* reg_left */
			       PCM1796_REG17_ATR, /* reg_right */
			       PCM1796_REG16_ATL_SHIFT, /* xshift */
			       0x0F, /* xmin */
			       0xFF, /* xmax */
			       0, /* xinvert */
			       pcm1796_dac_tlv /* tlv_array */ ),
#ifdef PCM1796_MUTE_SWITCH
	/* Mute */
	SOC_SINGLE_EXT("Digital Playback Switch", /* xname */
		       -1, /* xreg */
		       0, /* shift */
		       1, /* xmax */
		       0, /* xinvert */
		       pcm1796_digital_playback_switch_get, /* xhandler_get */
		       pcm1796_digital_playback_switch_put), /* xhandler_put */
#endif /* PCM1796_MUTE_SWITCH */
	/* Output Phase Reversal */
	SOC_ENUM("Phase", pcm1796_polarity_enum),
	/* Digital Filter Roll-off Control */
	SOC_ENUM("Filter", pcm1796_filter_shape_enum),
	/* De-emphasis filter enable/disable */
	SOC_SINGLE("De-Em", PCM1796_REG18_DME, PCM1796_REG18_DME_SHIFT, 1, 0),
	/* De-emphasis filter frequency select */
	SOC_ENUM("De-Em Fq", pcm1796_deemph_select_enum),
	/* Attenuation Rate Select */
	SOC_ENUM("Atten Rate", pcm1796_atten_rate_select_enum),
	/* Infinite Zero Detect Mute Control */
	SOC_ENUM("InfZeroDetectMute", pcm1796_inf_zero_detect_enum),
};

static const struct snd_soc_dapm_widget pcm1796_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("IDACL+", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("IDACL-", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("IDACR+", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("IDACR-", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_OUTPUT("IOUTL+"),
	SND_SOC_DAPM_OUTPUT("IOUTL-"),
	SND_SOC_DAPM_OUTPUT("IOUTR+"),
	SND_SOC_DAPM_OUTPUT("IOUTR-"),
};

static const struct snd_soc_dapm_route pcm1796_dapm_routes[] = {
	{ "IDACL+", NULL, "Playback" },
	{ "IDACL-", NULL, "Playback" },
	{ "IDACR+", NULL, "Playback" },
	{ "IDACR-", NULL, "Playback" },

	{ "IOUTL+", NULL, "IDACL+" },
	{ "IOUTL-", NULL, "IDACL-" },
	{ "IOUTR+", NULL, "IDACR+" },
	{ "IOUTR-", NULL, "IDACR-" },
};

static const struct snd_soc_component_driver pcm1796_comp_drv = {
	.controls              = pcm1796_controls,
	.num_controls          = ARRAY_SIZE(pcm1796_controls),
	.dapm_widgets          = pcm1796_dapm_widgets,
	.num_dapm_widgets      = ARRAY_SIZE(pcm1796_dapm_widgets),
	.dapm_routes           = pcm1796_dapm_routes,
	.num_dapm_routes       = ARRAY_SIZE(pcm1796_dapm_routes),
	.idle_bias_on          = 1,
	.use_pmdown_time       = 1,
	.endianness            = 1,
	.non_legacy_dai_naming = 1,
};

static const struct snd_soc_dai_ops pcm1796_dai_ops = {
	.set_fmt         = pcm1796_dai_set_fmt,
	.set_bclk_ratio  = pcm1796_dai_set_bclk_ratio,
	.hw_params       = pcm1796_dai_hw_params,
	.mute_stream     = pcm1796_dai_mute_stream,
	.set_sysclk      = pcm1796_dai_set_sysclk,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
	.no_capture_mute = 1,
#endif
};

static struct snd_soc_dai_driver pcm1796_dai_drv = {
	.name     = "pcm1796-hifi",
	.playback = {
		.stream_name  = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates        = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min     = 10000,
		.rate_max     = 200000,
		.formats      = PCM1796_FORMATS,
	},
	.ops      = &pcm1796_dai_ops,
};

const struct regmap_config pcm1796_regmap_cfg = {
	.reg_bits         = 8,
	.val_bits         = 8,

	.writeable_reg    = pcm1796_writeable_reg,
	.readable_reg     = pcm1796_accessible_reg,
	.volatile_reg     = pcm1796_volatile_reg,

	.cache_type       = REGCACHE_RBTREE,
	.max_register     = PCM1796_REG23,
	.reg_defaults     = pcm1796_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(pcm1796_reg_defaults),
};
EXPORT_SYMBOL_GPL(pcm1796_regmap_cfg);

int pcm1796_probe(struct device *dev, struct regmap *regmap)
{
	int ret;
	struct pcm1796_drvdata *data;

	dev_dbg(dev, "%s: ENTER\n", __func__);

	/*
	 * allocate memory for private data
	 */
#ifdef DDEBUG
	dev_dbg(dev, "%s: allocate memory for private data\n", __func__);
#endif /* DDEBUG */
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(dev, "%s: EXIT [-ENOMEM]: failed to allocate memory "
			"for private data: returning [-ENOMEM]\n", __func__);
		return -ENOMEM;
	}

	mutex_init(&data->mutex);
	dev_set_drvdata(dev, data);

	/*
	 * optional mute gpio
	 * NB. gpio default is active low
	 */
#ifdef DDEBUG
	dev_dbg(dev, "%s: devm_gpiod_get_optional(mute, "
		"PCM1796_GPIOD_OUT_LOW)\n", __func__);
#endif /* DDEBUG */
	data->mute_gpio = devm_gpiod_get_optional(dev, "mute",
						  PCM1796_GPIOD_OUT_LOW);
	if (IS_ERR(data->mute_gpio)) {
		ret = PTR_ERR(data->mute_gpio);
		if (ret == -EPROBE_DEFER)
			dev_info(dev, "%s: devm_gpiod_get_optional(mute) "
				 "returns: [-EPROBE_DEFER]\n", __func__);
		else
			dev_err(dev, "%s: devm_gpiod_get_optional(mute) "
				"failed: [%d]\n", __func__, ret);
		return ret;
	}
#ifdef DDEBUG
	if (data->mute_gpio)
		dev_dbg(dev, "%s: obtained reference to optional mute gpio\n",
			__func__);
	else
		dev_dbg(dev, "%s: did not obtain reference to optional mute "
			"gpio\n", __func__);
#endif /* DDEBUG */

	/*
	 * CLOCK
	 */
#ifdef DDEBUG
	dev_dbg(dev, "%s: devm_clk_get(sclk)\n", __func__);
#endif /* DDEBUG */
	data->sclk = devm_clk_get(dev, "sclk");
	if (IS_ERR(data->sclk)) {
		ret = PTR_ERR(data->sclk);
		if (ret == -EPROBE_DEFER)
			dev_info(dev, "%s: EXIT [-EPROBE_DEFER]: "
				 "devm_clk_get(sclk) returns: "
				 "[-EPROBE_DEFER]\n", __func__);
		else
			dev_err(dev, "%s: EXIT [%d]: devm_clk_get(sclk) "
				"failed!\n", __func__, ret);
		return ret;
	}

#ifdef DDEBUG
	dev_dbg(dev, "%s: clk_prepare_enable(sclk)\n", __func__);
#endif /* DDEBUG */
	ret = clk_prepare_enable(data->sclk);
	if (ret < 0) {
		dev_err(dev, "%s: EXIT [%d]: clk_prepare_enable(sclk) "
			"failed!\n", __func__, ret);
		return ret;
	}

	/*
	 * RESET GPIO
	 * NB. gpio default is active low
	 */
#ifdef DDEBUG
	dev_dbg(dev, "%s: devm_gpiod_get(reset, PCM1796_GPIOD_OUT_LOW)\n",
		__func__);
#endif /* DDEBUG */
	data->reset_gpio = devm_gpiod_get(dev, "reset", PCM1796_GPIOD_OUT_LOW);
	if (IS_ERR(data->reset_gpio)) {
		ret = PTR_ERR(data->reset_gpio);
		if (ret == -EPROBE_DEFER)
			dev_info(dev, "%s: devm_gpiod_get(reset) returns: "
				 "[-EPROBE_DEFER]\n", __func__);
		else
			dev_err(dev, "%s: devm_gpiod_get(reset) failed: [%d]\n",
				__func__, ret);
		goto clk_err;
	}

	/*
	 * RESET the pcm1796 using the reset gpio
	 */
#ifdef DDEBUG
	dev_dbg(dev, "%s: reset using reset gpio\n", __func__);
#endif /* DDEBUG */
	/*
	 * The RST pin is set to logic 0 for a minimum of 20 ns.
	 */
	gpiod_set_raw_value_cansleep(data->reset_gpio, 0);
	udelay(1);
	/*
	 * The RST pin is then set to a logic 1 state, thus starting the 
	 * initialization sequence, which requires 1024 system clock periods.
	 */
	gpiod_set_raw_value_cansleep(data->reset_gpio, 1);
//	usleep_range(46, 50); // (wait for 46us with a 22M5792 sysclk)
	udelay(DIV_ROUND_UP(1024 * 1000000, clk_get_rate(data->sclk)));

	/*
	 * allow writing to the pcm1796 volume attenuation registers
	 */
#ifdef DDEBUG
	dev_dbg(dev, "%s: set REG18_ATLD_ENABLE (allow writing to volume "
		"attenuation registers)\n", __func__);
#endif /* DDEBUG */
	ret = regmap_update_bits(regmap,
				 PCM1796_REG18_ATLD,
				 PCM1796_REG18_ATLD_MASK,
				 PCM1796_REG18_ATLD_ENABLE);
	if (ret < 0)
		dev_warn(dev, "%s: failed to set REG18_ATLD_ENABLE: [%d]\n",
			 __func__, ret);

	/*
	 * mute dac
	 *
	 * NB. Will be auto enabled/disabled from pcm1796_dai_mute_stream()
	 */
#ifdef DDEBUG
	dev_dbg(dev, "%s: set REG18_MUTE_ENABLE (mute)\n", __func__);
#endif /* DDEBUG */
	ret = regmap_update_bits(regmap,
				 PCM1796_REG18_MUTE,
				 PCM1796_REG18_MUTE_MASK,
				 PCM1796_REG18_MUTE_ENABLE);
	if (ret < 0)
		dev_warn(dev, "%s: failed to set REG18_MUTE_ENABLE: "
			 "[%d]\n", __func__, ret);

	/*
	 * disable analogue output
	 *
	 * NB. Will be auto enabled/disabled from pcm1796_dai_mute_stream()
	 */
	dev_dbg(dev, "%s: set REG19_OPE_DISABLE (disable analogue output)\n",
		__func__);
	ret = regmap_update_bits(regmap,
				 PCM1796_REG19_OPE,
				 PCM1796_REG19_OPE_MASK,
				 PCM1796_REG19_OPE_DISABLE);
	if (ret < 0)
		dev_warn(dev, "%s: failed to set REG19_OPE_DISABLE: [%d]\n",
			 __func__, ret);

	/*
	 * !auto_gpio_mute: one-time gpio unmute
	 */
	if (data->mute_gpio) {
		if (dev->of_node)
			data->auto_gpio_mute = of_property_read_bool(
					dev->of_node, "pcm1796,auto-gpio-mute");
		if (!data->auto_gpio_mute) {
#ifdef DDEBUG
			dev_dbg(dev, "%s: !auto_gpio_mute: unmute: "
				"gpiod_set_raw_value_cansleep(mute, 1)\n",
				__func__);
#endif /* DDEBUG */
			gpiod_set_raw_value_cansleep(data->mute_gpio, 1);
		}
	}

	/*
	 * register component
	 */
#ifdef DDEBUG
        dev_dbg(dev, "%s: devm_snd_soc_register_component()\n", __func__);
#endif /* DDEBUG */
	ret = devm_snd_soc_register_component(dev, &pcm1796_comp_drv,
					      &pcm1796_dai_drv, 1);
	if (ret < 0) {
		if (ret == -EPROBE_DEFER)
			dev_info(dev, "%s: devm_snd_soc_register_component() "
				 "returns: [-EPROBE_DEFER]\n", __func__);
		else
			dev_err(dev, "%s: devm_snd_soc_register_component() "
				"failed: [%d]\n", __func__, ret);
		goto gpio_err;
        }

        dev_dbg(dev, "%s: EXIT [0]\n", __func__);

	return 0;

gpio_err:
	if (data->mute_gpio && !data->auto_gpio_mute) {
#ifdef DDEBUG
		dev_dbg(dev, "%s: mute: "
			"gpiod_set_raw_value_cansleep(mute, 0)\n", __func__);
#endif /* DDEBUG */
		gpiod_set_raw_value_cansleep(data->mute_gpio, 0);
	}

clk_err:
#ifdef DDEBUG
	dev_dbg(dev, "%s: clk_disable_unprepare(sclk)\n", __func__);
#endif /* DDEBUG */
	clk_disable_unprepare(data->sclk);

	if (ret < 0) {
		if (ret == -EPROBE_DEFER)
			dev_info(dev, "%s: EXIT [-EPROBE_DEFER]\n", __func__);
		else
			dev_err(dev, "%s: EXIT [%d]\n", __func__, ret);
	} else
		dev_dbg(dev, "%s: EXIT [%d]\n", __func__, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(pcm1796_probe);

void pcm1796_remove(struct device *dev)
{
	struct pcm1796_drvdata *data = dev_get_drvdata(dev);

        dev_dbg(dev, "%s: ENTER\n", __func__);

        /* gpio mute */
        if (data->mute_gpio) {
#ifdef DDEBUG
		dev_dbg(dev, "%s: mute: "
			"gpiod_set_raw_value_cansleep(mute, 0)\n", __func__);
#endif
		gpiod_set_raw_value_cansleep(data->mute_gpio, 0);
	}

	/* disable/unprepare clock */
#ifdef DDEBUG
	dev_dbg(dev, "%s: clk_disable_unprepare(sclk)\n", __func__);
#endif /* DDEBUG */
	clk_disable_unprepare(data->sclk);

	/* put DAC into RESET */
#ifdef DDEBUG
	dev_dbg(dev, "%s: put into reset using reset_gpio\n", __func__);
#endif /* DDEBUG */
	gpiod_set_raw_value_cansleep(data->reset_gpio, 0);

	dev_dbg(dev, "%s: EXIT [void]\n", __func__);
}
EXPORT_SYMBOL_GPL(pcm1796_remove);

MODULE_DESCRIPTION("ALTernative ASoC PCM1796 codec driver");
MODULE_AUTHOR("Michael Trimarchi <michael@amarulasolutions.com>");
MODULE_AUTHOR("Clive Messer <clive.messer@digitaldreamtime.co.uk>");
MODULE_LICENSE("GPL");
