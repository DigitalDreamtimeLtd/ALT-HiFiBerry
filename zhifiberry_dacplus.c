// SPDX-License-Identifier: GPL-2.0
/*
 * ASoC Driver for HiFiBerry DAC+ / DAC Pro / AMP100
 *
 * Author:	Daniel Matuschek, Stuart MacLean <stuart@hifiberry.com>
 *		Copyright 2014-2015
 *		based on code by Florian Meier <florian.meier@koalo.de>
 *		Headphone/AMP100 Joerg Schambacher <joerg@hifiberry.com>
 *
 * Author: Clive Messer <clive.messer@digitaldreamtime.co.uk>
 *         Copyright (c) Digital Dreamtime Ltd 2016-2021
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

#include <linux/module.h>
#include <linux/gpio/consumer.h>
//#include <../drivers/gpio/gpiolib.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/version.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>

#include "zpcm512x.h"

#define DRV_VERSION "4.0.0"

#define HIFIBERRY_DACPRO_NOCLOCK 0
#define HIFIBERRY_DACPRO_CLK44EN 1
#define HIFIBERRY_DACPRO_CLK48EN 2

/* Clock rate of CLK44EN attached to GPIO6 pin */
#define CLK_44EN_RATE 22579200UL
/* Clock rate of CLK48EN attached to GPIO3 pin */
#define CLK_48EN_RATE 24576000UL

struct zpcm512x_priv {
	struct regmap *regmap;
	struct clk *sclk;
};

static bool slave;
static bool snd_rpi_hb_is_dacpro;
static bool digital_gain_0db_limit = true;
static bool leds_off;
static bool auto_mute;
static int mute_ext_ctl;
static int mute_ext;
static struct gpio_desc *snd_mute_gpio;
static struct gpio_desc *snd_reset_gpio;
//static struct snd_soc_card snd_rpi_hifiberry_dacplus;

static int snd_rpi_hifiberry_dacplus_mute_set(int mute)
{
	gpiod_set_value_cansleep(snd_mute_gpio, mute);
	return 1;
}

static int snd_rpi_hifiberry_dacplus_mute_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = mute_ext;

	return 0;
}

static int snd_rpi_hifiberry_dacplus_mute_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	if (mute_ext == ucontrol->value.integer.value[0])
		return 0;

	mute_ext = ucontrol->value.integer.value[0];

	return snd_rpi_hifiberry_dacplus_mute_set(mute_ext);
}

static const char * const mute_text[] = {"Play", "Mute"};
static const struct soc_enum hb_dacplus_opt_mute_enum =
					SOC_ENUM_SINGLE_EXT(2, mute_text);

static const struct snd_kcontrol_new hb_dacplus_opt_mute_controls[] = {
	SOC_ENUM_EXT("Mute(ext)", hb_dacplus_opt_mute_enum,
	snd_rpi_hifiberry_dacplus_mute_get, snd_rpi_hifiberry_dacplus_mute_put),
};

static void snd_rpi_hb_dacplus_select_clk(
			struct snd_soc_pcm_runtime *soc_runtime, int clk_id)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
	struct snd_soc_component *component =
				asoc_rtd_to_codec(soc_runtime, 0)->component;
#else
	struct snd_soc_component *component = soc_runtime->codec_dai->component;
#endif
	struct device *dev = soc_runtime->card->dev;

	dev_dbg(dev, "%s: ENTER\n", __func__);

	switch (clk_id) {
	case HIFIBERRY_DACPRO_NOCLOCK:
		dev_dbg(dev, "%s: selecting NOCLOCK\n", __func__);
		/* GPIO3 and GPIO6 output low */
		snd_soc_component_update_bits(component, PCM512x_GPIO_CONTROL_1,
					      0x24, 0x00);
		break;
	case HIFIBERRY_DACPRO_CLK44EN:
		dev_dbg(dev, "%s: selecting CLK44EN\n", __func__);
		/* GPIO6 output high */
		snd_soc_component_update_bits(component, PCM512x_GPIO_CONTROL_1,
					      0x24, 0x20);
		break;
	case HIFIBERRY_DACPRO_CLK48EN:
		dev_dbg(dev, "%s: selecting CLK48EN\n", __func__);
		/* GPIO3 output high */
		snd_soc_component_update_bits(component, PCM512x_GPIO_CONTROL_1,
					      0x24, 0x04);
		break;
	}

#ifdef DDEBUG
	dev_dbg(dev, "%s: sleeping... usleep_range(2000, 2100)\n", __func__);
#endif /* DDEBUG */
	usleep_range(2000, 2100);

	dev_dbg(dev, "%s: EXIT [void]\n", __func__);
}

static void snd_rpi_hb_dacplus_clk_gpio(struct snd_soc_pcm_runtime *soc_runtime)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
	struct snd_soc_component *component =
				asoc_rtd_to_codec(soc_runtime, 0)->component;
#else
	struct snd_soc_component *component = soc_runtime->codec_dai->component;
#endif
	struct device *dev = soc_runtime->card->dev;

	dev_dbg(dev, "%s: ENTER\n", __func__);

	/* set the direction of GPIO3 and GPIO6 as outputs */
	snd_soc_component_update_bits(component, PCM512x_GPIO_EN, 0x24, 0x24);
	/* Register GPIO3 output */
	snd_soc_component_update_bits(component, PCM512x_GPIO_OUTPUT_3,
				      0x0f, 0x02);
	/* Register GPIO6 output */
	snd_soc_component_update_bits(component, PCM512x_GPIO_OUTPUT_6,
				      0x0f, 0x02);

	dev_dbg(dev, "%s: EXIT [void]\n", __func__);
}

static bool snd_rpi_hb_dacplus_is_sclk(struct snd_soc_pcm_runtime *soc_runtime)
{
	unsigned int sck;
	bool is_sclk;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
	struct snd_soc_component *component =
				asoc_rtd_to_codec(soc_runtime, 0)->component;
#else
	struct snd_soc_component *component = soc_runtime->codec_dai->component;
#endif
	struct device *dev = soc_runtime->card->dev;

	dev_dbg(dev, "%s: ENTER\n", __func__);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
	sck = snd_soc_component_read(component, PCM512x_RATE_DET_4);
#else
	sck = snd_soc_component_read32(component, PCM512x_RATE_DET_4);
#endif
	/* CDST: This bit indicates whether the SCK clock is present or not. */
	is_sclk = !(sck & 0x40);

	dev_dbg(dev, "%s: EXIT [%s]\n", __func__, is_sclk ? "true" : "false");

	return (is_sclk);
}

static bool snd_rpi_hb_dacplus_is_pro_card(
	struct snd_soc_pcm_runtime *soc_runtime)
{
	bool isClk44EN, isClk48En, isNoClk, isPro;
	struct device *dev = soc_runtime->card->dev;

	dev_dbg(dev, "%s: ENTER\n", __func__);

	snd_rpi_hb_dacplus_clk_gpio(soc_runtime);

	snd_rpi_hb_dacplus_select_clk(soc_runtime, HIFIBERRY_DACPRO_CLK44EN);
	isClk44EN = snd_rpi_hb_dacplus_is_sclk(soc_runtime);

	snd_rpi_hb_dacplus_select_clk(soc_runtime, HIFIBERRY_DACPRO_NOCLOCK);
	isNoClk = snd_rpi_hb_dacplus_is_sclk(soc_runtime);

	snd_rpi_hb_dacplus_select_clk(soc_runtime, HIFIBERRY_DACPRO_CLK48EN);
	isClk48En = snd_rpi_hb_dacplus_is_sclk(soc_runtime);

	isPro = isClk44EN && isClk48En && !isNoClk;

	dev_dbg(dev, "%s: EXIT [%s]\n", __func__, isPro ? "true" : "false");

	return isPro;
}

static int snd_rpi_hb_dacplus_clk_for_rate(
	struct snd_soc_pcm_runtime *soc_runtime, int sample_rate)
{
	int type;
	struct device *dev = soc_runtime->card->dev;

	dev_dbg(dev, "%s: ENTER: sample_rate=%d\n", __func__, sample_rate);
	
	switch (sample_rate) {
	case 11025:
	case 22050:
	case 44100:
	case 88200:
	case 176400:
	case 352800:
		type = HIFIBERRY_DACPRO_CLK44EN;
		break;
	default:
		type = HIFIBERRY_DACPRO_CLK48EN;
		break;
	}
	
	dev_dbg(dev, "%s: EXIT [%s]\n", __func__,
		(type == HIFIBERRY_DACPRO_CLK44EN) ? "CLK_44EN_RATE"
						   : "CLK_48EN_RATE");	

	return type;
}

static void snd_rpi_hb_dacplus_set_sclk(struct snd_soc_pcm_runtime *soc_runtime,
					int sample_rate)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
	struct snd_soc_component *component =
				asoc_rtd_to_codec(soc_runtime, 0)->component;
#else
	struct snd_soc_component *component = soc_runtime->codec_dai->component;
#endif
	struct zpcm512x_priv *priv = snd_soc_component_get_drvdata(component);
	struct device *dev = soc_runtime->card->dev;
	unsigned long clock_rate;
	
	dev_dbg(dev, "%s: ENTER\n", __func__);

	if (!IS_ERR(priv->sclk)) {
		int ctype;

		ctype = snd_rpi_hb_dacplus_clk_for_rate(soc_runtime,
							sample_rate);
		clock_rate = (ctype == HIFIBERRY_DACPRO_CLK44EN)
					? CLK_44EN_RATE : CLK_48EN_RATE;
		dev_dbg(dev, "%s: clk_set_rate(%lu)\n", __func__, clock_rate);
		clk_set_rate(priv->sclk, clock_rate);
		snd_rpi_hb_dacplus_select_clk(soc_runtime, ctype);
	}

	dev_dbg(dev, "%s: EXIT [void]\n", __func__);
}

static int snd_rpi_hb_dacplus_init(struct snd_soc_pcm_runtime *soc_runtime)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
	struct snd_soc_component *component =
				asoc_rtd_to_codec(soc_runtime, 0)->component;
#else
	struct snd_soc_component *component = soc_runtime->codec_dai->component;
#endif
	struct device *dev = soc_runtime->card->dev;
	struct zpcm512x_priv *priv;
//	struct snd_soc_card *card = &snd_rpi_hifiberry_dacplus;
	struct snd_soc_card *card = soc_runtime->card;

	dev_dbg(dev, "%s: ENTER\n", __func__);

	if (slave)
		snd_rpi_hb_is_dacpro = false;
	else
		snd_rpi_hb_is_dacpro =
				snd_rpi_hb_dacplus_is_pro_card(soc_runtime);

	if (snd_rpi_hb_is_dacpro) {
		struct snd_soc_dai_link *dai = soc_runtime->dai_link;

		dai->name = "HiFiBerry DAC+ Pro";
		dai->stream_name = "HiFiBerry DAC+ Pro HiFi";
		dai->dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
					| SND_SOC_DAIFMT_CBM_CFM;

		snd_soc_component_update_bits(component,
					      PCM512x_BCLK_LRCLK_CFG,
					      0x31, 0x11);
		snd_soc_component_update_bits(component,
					      PCM512x_MASTER_MODE,
					      0x03, 0x03);
		snd_soc_component_update_bits(component,
					      PCM512x_MASTER_CLKDIV_2,
					      0x7f, 63);
	} else {
		priv = snd_soc_component_get_drvdata(component);
		priv->sclk = ERR_PTR(-ENOENT);
	}

	snd_soc_component_update_bits(component, PCM512x_GPIO_EN, 0x08, 0x08);
	snd_soc_component_update_bits(component, PCM512x_GPIO_OUTPUT_4,
				      0x0f, 0x02);
	if (leds_off)
		snd_soc_component_update_bits(component, PCM512x_GPIO_CONTROL_1,
					      0x08, 0x00);
	else
		snd_soc_component_update_bits(component, PCM512x_GPIO_CONTROL_1,
					      0x08, 0x08);

	if (digital_gain_0db_limit) {
		int ret;
		struct snd_soc_card *card = soc_runtime->card;

		ret = snd_soc_limit_volume(card, "Digital Playback Volume",
					   207);
		if (ret < 0)
			dev_warn(dev, "%s: failed to set volume limit: %d\n",
				 __func__, ret);
	}

	if (snd_reset_gpio) {
		gpiod_set_value_cansleep(snd_reset_gpio, 0);
//		msleep(1);
		usleep_range(1000, 1100);
		gpiod_set_value_cansleep(snd_reset_gpio, 1);
//		msleep(1);
		usleep_range(1000, 1100);
		gpiod_set_value_cansleep(snd_reset_gpio, 0);
	}

	if (mute_ext_ctl)
		snd_soc_add_card_controls(card, hb_dacplus_opt_mute_controls,
				ARRAY_SIZE(hb_dacplus_opt_mute_controls));

	if (snd_mute_gpio)
		gpiod_set_value_cansleep(snd_mute_gpio, mute_ext);

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);

	return 0;
}

static int snd_rpi_hb_dacplus_update_rate_den(
	struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
	struct snd_interval *interval =
			hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
	struct snd_soc_component *component =
				asoc_rtd_to_codec(soc_runtime, 0)->component;
#else
	struct snd_soc_component *component = soc_runtime->codec_dai->component;
#endif
	struct zpcm512x_priv *priv = snd_soc_component_get_drvdata(component);
	struct device *dev = soc_runtime->card->dev;
	struct snd_ratnum *rats_no_pll;
	unsigned int num = 0, den = 0;
	int err;

	dev_dbg(dev, "%s: ENTER: params->rate_num=%d, params->rate_den=%d\n",
		__func__, params->rate_num, params->rate_den);

	rats_no_pll = devm_kzalloc(soc_runtime->dev, sizeof(*rats_no_pll),
				   GFP_KERNEL);
	if (!rats_no_pll) {
		dev_err(dev, "%s: EXIT [-ENOMEM]: failed to allocate memory "
			"for rats_no_pll!\n", __func__);
		return -ENOMEM;
	}

	rats_no_pll->num = clk_get_rate(priv->sclk) / 64;
	rats_no_pll->den_min = 1;
	rats_no_pll->den_max = 128;
	rats_no_pll->den_step = 1;

        dev_dbg(component->dev, "%s: refine ratnum interval value: num=%d, "
        	"den_min=%d, den_max=%d, den_step=%d\n", __func__,
        	rats_no_pll->num, rats_no_pll->den_min, rats_no_pll->den_max, 
        	rats_no_pll->den_step);

	err = snd_interval_ratnum(interval, 1, rats_no_pll, &num, &den);
	if (err >= 0 && den) {
		dev_dbg(component->dev, "%s: setting params->rate_num=%d, " 
			"params->rate_den=%d\n", __func__, num, den);
		params->rate_num = num;
		params->rate_den = den;
	}

	devm_kfree(soc_runtime->dev, rats_no_pll);
	dev_dbg(dev, "%s: EXIT [0]\n", __func__);

	return 0;
}

static int snd_rpi_hb_dacplus_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	int ret = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
	struct snd_soc_pcm_runtime *soc_runtime =
					asoc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(soc_runtime, 0);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(soc_runtime, 0);
#else
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct snd_soc_dai *cpu_dai = soc_runtime->cpu_dai;
	struct snd_soc_dai *codec_dai = soc_runtime->codec_dai;
#endif
	struct device *dev = soc_runtime->card->dev;
	int channels = params_channels(params);
	int width = 32;
	unsigned int bclk_ratio;

	snd_pcm_format_t format = params_format(params);
	unsigned int rate = params_rate(params);

        dev_dbg(dev, "%s: ENTER: frequency=%u, format=%s, "
                "sample_bits=%u, physical_bits=%u, channels=%u\n", __func__,
                rate, snd_pcm_format_name(format),
                snd_pcm_format_width(format),
                snd_pcm_format_physical_width(format),
                channels);

	if (snd_rpi_hb_is_dacpro) {
		width = snd_pcm_format_physical_width(params_format(params));

		snd_rpi_hb_dacplus_set_sclk(soc_runtime, params_rate(params));

		ret = snd_rpi_hb_dacplus_update_rate_den(substream, params);
	}

	bclk_ratio = channels * width;
	
	dev_dbg(dev, "%s: setting cpu bclk_ratio=%d\n", __func__,
		channels * width);
	ret = snd_soc_dai_set_bclk_ratio(cpu_dai, bclk_ratio);
	if (ret) {
		dev_err(dev, "%s: EXIT [%d]: error setting cpu "
			"bclk_ratio=%d!\n", __func__, ret, bclk_ratio);
		return ret;
	}
	
	dev_dbg(dev, "%s: setting codec bclk_ratio=%d\n", __func__,
		channels * width);
	ret = snd_soc_dai_set_bclk_ratio(codec_dai, bclk_ratio);
	if (ret) {
		dev_err(dev, "%s: EXIT [%d]: error setting codec "
			"bclk_ratio=%d!\n", __func__, ret, bclk_ratio);
		return ret;
	}
	
	dev_dbg(dev, "%s: EXIT [%d]\n", __func__, ret);

	return ret;
}

static int snd_rpi_hb_dacplus_startup(struct snd_pcm_substream *substream)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
	struct snd_soc_pcm_runtime *soc_runtime =
					asoc_substream_to_rtd(substream);
	struct snd_soc_component *component =
				asoc_rtd_to_codec(soc_runtime, 0)->component;
#else
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct snd_soc_component *component = soc_runtime->codec_dai->component;
#endif
	struct device *dev = soc_runtime->card->dev;

	dev_dbg(dev, "%s: ENTER\n", __func__);

	if (auto_mute)
		gpiod_set_value_cansleep(snd_mute_gpio, 0);

	if (leds_off) {
		dev_dbg(dev, "%s: EXIT [0]: noop - leds_off\n", __func__);
		return 0;
	}

	snd_soc_component_update_bits(component, PCM512x_GPIO_CONTROL_1,
				      0x08, 0x08);
				      
	if (auto_mute)
		gpiod_set_value_cansleep(snd_mute_gpio, 1);

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);

	return 0;
}

static void snd_rpi_hb_dacplus_shutdown(struct snd_pcm_substream *substream)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
	struct snd_soc_pcm_runtime *soc_runtime =
					asoc_substream_to_rtd(substream);
	struct snd_soc_component *component =
				asoc_rtd_to_codec(soc_runtime, 0)->component;
#else
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct snd_soc_component *component = soc_runtime->codec_dai->component;
#endif
	struct device *dev = soc_runtime->card->dev;

	dev_dbg(dev, "%s: ENTER\n", __func__);

	snd_soc_component_update_bits(component, PCM512x_GPIO_CONTROL_1,
				      0x08, 0x00);

	if (auto_mute)
		gpiod_set_value_cansleep(snd_mute_gpio, 1);

	if (snd_rpi_hb_is_dacpro) {
		struct zpcm512x_priv *priv =
				snd_soc_component_get_drvdata(component);
		/*
		 * Default sclk to CLK_48EN_RATE, otherwise codec
		 * pcm512x_dai_startup_master method could call
		 * snd_pcm_hw_constraint_ratnums using CLK_44EN/64
		 * which will mask 384k sample rate.
		 */
		if (!IS_ERR(priv->sclk)) {
			dev_dbg(dev, "%s: clk_set_rate(%lu)\n", __func__,
				CLK_48EN_RATE);
			clk_set_rate(priv->sclk, CLK_48EN_RATE);
		}
	}

	dev_dbg(dev, "%s: EXIT [void]\n", __func__);
}

/* machine stream operations */
static struct snd_soc_ops dacplus_ops = {
	.hw_params = snd_rpi_hb_dacplus_hw_params,
	.startup   = snd_rpi_hb_dacplus_startup,
	.shutdown  = snd_rpi_hb_dacplus_shutdown,
};

SND_SOC_DAILINK_DEFS(dacplus_dailink_component,
	DAILINK_COMP_ARRAY(COMP_CPU("bcm2708-i2s.0")),
	DAILINK_COMP_ARRAY(COMP_CODEC("zpcm512x.1-004d", "zpcm512x-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("bcm2708-i2s.0")));

static struct snd_soc_dai_link dacplus_dai_link[] = {
{
	.name        = "HiFiBerry DAC+",
	.stream_name = "HiFiBerry DAC+ HiFi",
	.dai_fmt     = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
	.ops         = &dacplus_ops,
	.init        = snd_rpi_hb_dacplus_init,
	SND_SOC_DAILINK_REG(dacplus_dailink_component),
},
};

/* aux device for optional headphone amp */
static struct snd_soc_aux_dev dacplus_aux_devs[] = {
	{
		.dlc = {
			.name = "tpa6130a2.1-0060",
		},
	},
};

/* audio machine driver */
static struct snd_soc_card dacplus_card = {
	.name        = "HiFiBerry DACplus",
	.driver_name = "HiFiBerryDACplus",
	.owner       = THIS_MODULE,
	.dai_link    = dacplus_dai_link,
	.num_links   = ARRAY_SIZE(dacplus_dai_link),
};

static int snd_rpi_hb_dacplus_hp_detect(void)
{
	struct i2c_adapter *adap = i2c_get_adapter(1);
	int ret;

	struct i2c_client tpa_i2c_client = {
		.addr = 0x60,
		.adapter = adap,
	};

	if (!adap)
		return -EPROBE_DEFER; /* I2C module not yet available */

	ret = i2c_smbus_read_byte(&tpa_i2c_client) >= 0;
	i2c_put_adapter(adap);

	return ret;
};

static struct property tpa_enable_prop = {
	.name   = "status",
	.length = 4 + 1, /* length 'okay' + 1 */
	.value  = "okay",
};

static int snd_rpi_hb_dacplus_probe(struct platform_device *pdev)
{
	int ret = 0, len;
	struct device *dev = &pdev->dev;
	struct device_node *i2s_node, *tpa_node = NULL;
	struct property *tpa_prop;
	struct of_changeset ocs;
	struct property *pp;
	int tmp;

	dev_dbg(dev, "%s: ENTER\n", __func__);

	/* probe for head phone amp */
#ifdef DDEBUG
	dev_dbg(dev, "%s: probing I2C for headphone amplifier\n", __func__);
#endif /* DDEBUG */
	ret = snd_rpi_hb_dacplus_hp_detect();
	if (ret < 0) {
		if (ret == -EPROBE_DEFER)
			dev_info(dev, "%s: EXIT [-EPROBE_DEFER]: "
				 "waiting for I2C to probe headphone amp\n",
				 __func__);
		else
			dev_err(dev, "%s: EXIT [%d]: failed to probe headphone "
				"amp!\n", __func__, ret);

		return ret;
	}
	if (ret) {
		dacplus_card.aux_dev = dacplus_aux_devs;
		dacplus_card.num_aux_devs = ARRAY_SIZE(dacplus_aux_devs);
		tpa_node = of_find_compatible_node(NULL, NULL, "ti,tpa6130a2");
		tpa_prop = of_find_property(tpa_node, "status", &len);

		if (strcmp((char *)tpa_prop->value, "okay")) {
			/* and activate headphone using change_sets */
			dev_info(dev, "%s: activating headphone amplifier\n",
				 __func__);
			of_changeset_init(&ocs);
			ret = of_changeset_update_property(&ocs, tpa_node,
							   &tpa_enable_prop);
			if (ret) {
				ret = -ENODEV;
				dev_err(dev, "%s: error activating headphone "
					"amplifier: returning [-ENODEV]\n",
					__func__);
				goto tpa_err;
			}
			ret = of_changeset_apply(&ocs);
			if (ret) {
				ret = -ENODEV;
				dev_err(dev, "%s: error activating headphone "
					"amplifier: returning [-ENODEV]\n",
					__func__);
				goto tpa_err;
			}
		}
	}
#ifdef DDEBUG
	  else
		dev_dbg(dev, "%s: did not detect headphone amp\n", __func__);
#endif /* DDEBUG */

	dacplus_card.dev = dev;

	if (dev->of_node) {
		struct snd_soc_dai_link *dai_link;
#ifdef DDEBUG
		dev_dbg(dev, "%s: get ref to i2s-controller from DT node\n",
			__func__);
#endif /* DDEBUG */
		i2s_node = of_parse_phandle(dev->of_node, "i2s-controller", 0);
		if (i2s_node) {
			dai_link = &dacplus_dai_link[0];

			dai_link->cpus->dai_name = NULL;
			dai_link->cpus->of_node = i2s_node;
			dai_link->platforms->name = NULL;
			dai_link->platforms->of_node = i2s_node;
		} else {
			ret = -ENODEV;
			dev_err(dev, "%s: failed to get reference to "
				"i2s-controller DT node: returning [-ENODEV]\n",
				__func__);
			goto tpa_err;
                }

		digital_gain_0db_limit = !of_property_read_bool(dev->of_node,
						"hifiberry,24db_digital_gain");
		slave = of_property_read_bool(dev->of_node,
						"hifiberry-dacplus,slave");
		leds_off = of_property_read_bool(dev->of_node,
						"hifiberry-dacplus,leds_off");
		auto_mute = of_property_read_bool(dev->of_node,
						"hifiberry-dacplus,auto_mute");

		/*
		 * check for HW MUTE as defined in DT-overlay
		 * active high, therefore default to HIGH to MUTE
		 */
#ifdef DDEBUG
		dev_dbg(dev, "%s: devm_gpiod_get_optional(mute, "
			"GPIOD_OUT_HIGH)\n", __func__);
#endif /* DDEBUG */
		snd_mute_gpio = devm_gpiod_get_optional(dev, "mute",
							GPIOD_OUT_HIGH);
		if (IS_ERR(snd_mute_gpio)) {
			ret = PTR_ERR(snd_mute_gpio);
//			dev_err(dev, "%s: Can't allocate GPIO (HW-MUTE)!\n",
//				__func__);
			if (ret == -EPROBE_DEFER)
				dev_info(dev, "%s: devm_gpiod_get_optional("
					 "mute) returns: [-EPROBE_DEFER]\n",
					 __func__);
			else
				dev_err(dev, "%s: devm_gpiod_get_optional("
					"mute) failed: [%d]\n", __func__, ret);
			goto i2s_err;
		}
#ifdef DDEBUG
		if (snd_mute_gpio)
			dev_dbg(dev, "%s: obtained reference to optional mute "
				"gpio\n", __func__);
		else
			dev_dbg(dev, "%s: did not obtain reference to optional "
				"mute gpio\n", __func__); 
#endif /* DDEBUG */
		/* add ALSA control if requested in DT-overlay (AMP100) */
		pp = of_find_property(dev->of_node,
				      "hifiberry-dacplus,mute_ext_ctl", &tmp);
		if (pp) {
			if (!of_property_read_u32(dev->of_node,
				"hifiberry-dacplus,mute_ext_ctl", &mute_ext)) {
				/* ALSA control will be used */
				mute_ext_ctl = 1;
			}
		}

		/* check for HW RESET (AMP100) */
#ifdef DDEBUG
		dev_dbg(dev, "%s: devm_gpiod_get_optional(reset, "
			"GPIOD_OUT_HIGH)\n", __func__);
#endif /* DDEBUG */
		snd_reset_gpio = devm_gpiod_get_optional(dev, "reset",
							 GPIOD_OUT_HIGH);
		if (IS_ERR(snd_reset_gpio)) {
			ret = PTR_ERR(snd_reset_gpio);
//			dev_err(dev, "%s: Can't allocate GPIO (HW-RESET)!\n",
//				__func__);
			if (ret == -EPROBE_DEFER)
				dev_info(dev, "%s: devm_gpiod_get_optional("
					 "reset) returns: [-EPROBE_DEFER]\n",
					 __func__);
			else
				dev_err(dev, "%s: devm_gpiod_get_optional("
					"reset) failed: [%d]\n", __func__, ret);
			goto i2s_err;
               }
#ifdef DDEBUG
		if (snd_reset_gpio)
			dev_dbg(dev, "%s: obtained reference to optional reset "
				"gpio\n", __func__);
		else
			dev_dbg(dev, "%s: did not obtain reference to optional "
				"reset gpio\n", __func__); 
#endif /* DDEBUG */
	} else {
		ret = -ENODEV;
		dev_err(dev, "%s: device tree node not found: returning "
			"[-ENODEV]\n", __func__);
		goto tpa_err;
        }

#ifdef DDEBUG
	dev_dbg(dev, "%s: snd_soc_register_card(%s)\n", __func__,
		dacplus_card.name);
#endif /* DDEBUG */
	ret = devm_snd_soc_register_card(dev, &dacplus_card);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "%s: snd_soc_register_card(%s) failed: "
				"[%d]\n", __func__, dacplus_card.name, ret);
		else
			dev_info(dev, "%s: snd_soc_register_card(%s) returns: "
				 "[-EPROBE_DEFER]\n", __func__,
				 dacplus_card.name);
		goto i2s_err;
	}
/*
	if (snd_mute_gpio) {
		dev_info(dev, "GPIO%i for HW-MUTE selected\n",
			 gpio_chip_hwgpio(snd_mute_gpio));
	}
	if (snd_reset_gpio) {
		dev_info(dev, "GPIO%i for HW-RESET selected\n",
			 gpio_chip_hwgpio(snd_reset_gpio));
	}
*/
	of_node_put(i2s_node);
	of_node_put(tpa_node);

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;

i2s_err:
	of_node_put(i2s_node);
tpa_err:
	of_node_put(tpa_node);

	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "%s: EXIT [%d]\n", __func__, ret);
		else
			dev_info(dev, "%s: EXIT [-EPROBE_DEFER]\n", __func__);
	} else
		dev_dbg(dev, "%s: EXIT [%d]\n", __func__, ret);

	return ret;
}

static const struct of_device_id dacplus_of_dev_ids[] = {
	{ .compatible = "hifiberry,dacplus", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, dacplus_of_dev_ids);

static struct platform_driver dacplus_platform_drv = {
	.driver = {
		.name           = "hifiberry-dacplus",
		.owner          = THIS_MODULE,
		.of_match_table = dacplus_of_dev_ids,
	},
	.probe  = snd_rpi_hb_dacplus_probe,
};
module_platform_driver(dacplus_platform_drv);

MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR("Daniel Matuschek <daniel@hifiberry.com>");
MODULE_AUTHOR("Clive Messer <clive.messer@digitaldreamtime.co.uk>");
MODULE_DESCRIPTION("ALTernative ASoC Driver for HiFiBerry DAC+");
MODULE_LICENSE("GPL v2");
