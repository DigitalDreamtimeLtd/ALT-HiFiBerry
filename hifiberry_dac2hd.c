// SPDX-License-Identifier: GPL-2.0
/*
 * ASoC Driver for HiFiBerry DAC2 HD
 *
 * Author:	Joerg Schambacher, i2Audio GmbH for HiFiBerry
 *		Copyright 2020
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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/version.h>

#include "pcm1796.h"

#define DRV_VERSION "5.2.1"

#define DEFAULT_RATE	44100
#define BCLK_RATIO	64

/* Clocks */
//#define CLK_RATE_44K1	22579200UL // 22.5792 MHz
//#define CLK_RATE_44K1	45158400UL // 45.1584 MHz
//#define CLK_RATE_48K	24576000UL // 24.576  MHz
//#define CLK_RATE_48K	49152000UL // 49.152  MHz

#ifdef DAC2HD_DRVDATA
struct dac2hd_drvdata {
};
#endif /* DAC2HD_DRVDATA */

static const char dac2hd_rates_texts[] = "44k1,48k,88k2,96k,172k6,192k";

static const unsigned int dac2hd_rates[] = {
	44100, 48000, 88200, 96000, 176400, 192000,
};

static struct snd_pcm_hw_constraint_list dac2hd_rates_constraint = {
	.list	= dac2hd_rates,
	.count	= ARRAY_SIZE(dac2hd_rates),
};

static int snd_rpi_hb_dac2hd_init(struct snd_soc_pcm_runtime *soc_runtime)
{
	int ret = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(soc_runtime, 0);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(soc_runtime, 0);
#else
	struct snd_soc_dai *cpu_dai = soc_runtime->cpu_dai;
	struct snd_soc_dai *codec_dai = soc_runtime->codec_dai;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0) */
	struct device *dev = soc_runtime->card->dev;

	dev_dbg(dev, "%s: ENTER\n", __func__);

	/*
	 * allow only fixed 32 clock counts per channel
	 */
#ifdef DDEBUG
	dev_dbg(dev, "%s: set cpu_dai bclk_ratio=%d\n", __func__, BCLK_RATIO);
#endif /* DDEBUG */
	ret = snd_soc_dai_set_bclk_ratio(cpu_dai, BCLK_RATIO);
	if (ret < 0) {
		dev_err(dev, "%s: EXIT [%d]: failed to set cpu_dai "
			"bclk_ratio=%d!\n", __func__, ret, BCLK_RATIO);
		return ret;
	}
#ifdef DDEBUG
	dev_dbg(dev, "%s: set codec_dai bclk_ratio=%d\n", __func__, BCLK_RATIO);
#endif /* DDEBUG */
	ret = snd_soc_dai_set_bclk_ratio(codec_dai, BCLK_RATIO);
	if (ret < 0) {
		dev_err(dev, "%s: EXIT [%d]: failed to set codec_dai "
			"bclk_ratio=%d!\n", __func__, ret, BCLK_RATIO);
		return ret;
	}

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static int snd_rpi_hb_dac2hd_startup(struct snd_pcm_substream *substream)
{
	int ret = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
	struct snd_soc_pcm_runtime *soc_runtime =
					asoc_substream_to_rtd(substream);
#else
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0) */
	struct device *dev = soc_runtime->card->dev;

	dev_dbg(dev, "%s: ENTER\n", __func__);

	/*
	 * constraints for standard sample rates
	 */
#ifdef DDEBUG
	dev_dbg(dev, "%s: set rates (%s) constraint\n", __func__,
		dac2hd_rates_texts);
#endif /* DDEBUG */
	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_RATE,
					 &dac2hd_rates_constraint);
	if (ret < 0) {
		dev_err(dev, "%s: EXIT [%d]: failed to set rates (%s) "
			"constraint!\n", __func__, ret, dac2hd_rates_texts);
		return ret;
	}

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static int snd_rpi_hb_dac2hd_set_osrate(struct snd_soc_pcm_runtime *soc_runtime,
					int sample_rate)
{
	int ret = 0, os_rate;
	char *os_rate_log;
	
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
	struct snd_soc_component *component =
				asoc_rtd_to_codec(soc_runtime, 0)->component;
#else
	struct snd_soc_component *component = soc_runtime->codec_dai->component;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0) */
	struct device *dev = soc_runtime->card->dev;

	dev_dbg(dev, "%s: ENTER: sample_rate=%d\n", __func__, sample_rate);

	if (sample_rate > 96000) {
		os_rate = PCM1796_REG20_OS_32;
		os_rate_log = "REG20_OS_32";
	} else if (sample_rate > 48000) {
		os_rate = PCM1796_REG20_OS_64;
		os_rate_log = "REG20_OS_64";
	} else /* if (sample_rate > 32000) */ {
		os_rate = PCM1796_REG20_OS_128;
		os_rate_log = "REG20_OS_128";
	} 
#ifdef DDEBUG
	dev_dbg(dev, "%s: set %s\n", __func__, os_rate_log);
#endif /* DDEBUG */
	ret = snd_soc_component_update_bits(component, PCM1796_REG20_OS,
					    PCM1796_REG20_OS_MASK, os_rate);
	if (ret) {
		if (ret < 0) {
			dev_err(dev, "%s: EXIT [%d]: failed to set %s!\n",
				 __func__, ret, os_rate_log);
			return ret;
		}
#ifdef DDEBUG
		  else
			dev_dbg(dev, "%s: set %s returns: [%d]\n",
				__func__, os_rate_log, ret);
#endif /* DDEBUG */
	}

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static int snd_rpi_hb_dac2hd_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	int ret = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
	struct snd_soc_pcm_runtime *soc_runtime =
					asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(soc_runtime, 0);
#else
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct snd_soc_dai *codec_dai = soc_runtime->codec_dai;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0) */
	struct device *dev = soc_runtime->card->dev;

	snd_pcm_format_t format = params_format(params);
	unsigned int sample_rate = params_rate(params);

	dev_dbg(dev, "%s: ENTER: frequency=%u, format=%s, sample_bits=%u, "
		"physical_bits=%u, channels=%u\n", __func__,
                sample_rate, snd_pcm_format_name(format),
                snd_pcm_format_width(format),
                snd_pcm_format_physical_width(format),
                params_channels(params));

	ret = snd_soc_dai_set_sysclk(codec_dai, PCM1796_SYSCLK_ID, 
				     sample_rate, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(dev, "%s: EXIT [%d]: snd_soc_dai_set_sysclk(%u) "
			"failed!\n", __func__, ret, sample_rate);
		return ret;
	}

	ret = snd_rpi_hb_dac2hd_set_osrate(soc_runtime, sample_rate);
	if (ret < 0) {
		dev_err(dev, "%s: EXIT [%d]: failed to set_osrate!\n",
			__func__, ret);
		return ret;
	}

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

/* machine stream operations */
static struct snd_soc_ops dac2hd_ops = {
	.startup   = snd_rpi_hb_dac2hd_startup,
	.hw_params = snd_rpi_hb_dac2hd_hw_params,
};

SND_SOC_DAILINK_DEFS(dac2hd_dailink_component,
	DAILINK_COMP_ARRAY(COMP_CPU("bcm2708-i2s.0")),
	DAILINK_COMP_ARRAY(COMP_CODEC("pcm1796.1-004c", "pcm1796-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("bcm2708-i2s.0")));

static struct snd_soc_dai_link dac2hd_dai_link[] = {
	{
		.name        = "HiFiBerry DAC2 HD",
		.stream_name = "HiFiBerry DAC2 HD HiFi",
		.dai_fmt     = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
					| SND_SOC_DAIFMT_CBM_CFM,
		.ops         = &dac2hd_ops,
		.init        = snd_rpi_hb_dac2hd_init,
		SND_SOC_DAILINK_REG(dac2hd_dailink_component),
	},
};

/* audio machine driver */
static struct snd_soc_card dac2hd_card = {
	.name           = "HiFiBerry DAC2HD",
	.driver_name    = "HiFiBerryDAC2HD",
	.owner          = THIS_MODULE,
	.dai_link       = dac2hd_dai_link,
	.num_links      = ARRAY_SIZE(dac2hd_dai_link),
};

static int snd_rpi_hb_dac2hd_probe(struct platform_device *pdev)
{
	int ret = 0;
#ifdef DAC2HD_DRVDATA
	struct dac2hd_drvdata *data;
#endif /* DAC2HD_DRVDATA */
	struct device *dev = &pdev->dev;
	struct device_node *i2s_node;

	dev_dbg(dev, "%s: ENTER\n", __func__);

	dac2hd_card.dev = dev;

#ifdef DAC2HD_DRVDATA
#ifdef DDEBUG
	dev_dbg(dev, "%s: allocate memory for private data\n", __func__);
#endif // DDEBUG
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data){
		dev_err(dev, "%s: EXIT [-ENOMEM]: failed to allocate memory "
			"for private driver data!\n",
			__func__);
		return -ENOMEM;;
	}

	snd_soc_card_set_drvdata(&dac2hd_card, data);
#endif /* DAC2HD_DRVDATA */

	/*
	 * DEVICE TREE
	 */
	if (dev->of_node) {
		struct snd_soc_dai_link *dai_link;
#ifdef DDEBUG
		dev_dbg(dev, "%s: get ref to i2s-controller from DT node\n",
			__func__);
#endif /* DDEBUG */
		i2s_node = of_parse_phandle(dev->of_node, "i2s-controller", 0);
		if (i2s_node) {
			dai_link = &dac2hd_dai_link[0];

			dai_link->cpus->of_node = i2s_node;
			dai_link->platforms->of_node = i2s_node;
			dai_link->cpus->dai_name = NULL;
			dai_link->platforms->name = NULL;
		} else {
			ret = -ENODEV;
			dev_err(dev, "%s: failed to get reference to "
				"i2s-controller DT node: returning [-ENODEV]\n",
				__func__);
			goto i2s_err;
		}
	} else {
		ret = -ENODEV;
		dev_err(dev, "%s: device tree node not found: returning "
			"[-ENODEV]\n", __func__);
		goto err;
	}

	/*
	 * REGISTER CARD
	 */
#ifdef DDEBUG
	dev_dbg(dev, "%s: snd_soc_register_card(%s)\n", __func__,
		dac2hd_card.name);
#endif /* DDEBUG */
	ret = devm_snd_soc_register_card(dev, &dac2hd_card);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "%s: snd_soc_register_card(%s) failed: "
				"[%d]\n", __func__, dac2hd_card.name, ret);
		else
			dev_info(dev, "%s: snd_soc_register_card(%s) returns: "
				 "[-EPROBE_DEFER]\n", __func__,
				 dac2hd_card.name);
		goto i2s_err;
	}
	
	of_node_put(i2s_node);

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;

i2s_err:
	of_node_put(i2s_node);
err:	
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "%s: EXIT [%d]\n", __func__, ret);
		else
			dev_info(dev, "%s: EXIT [-EPROBE_DEFER]\n", __func__);
	} else
		dev_dbg(dev, "%s: EXIT [%d]\n", __func__, ret);

	return ret;
}

static const struct of_device_id dac2hd_of_dev_ids[] = {
	{ .compatible = "hifiberry,dac2hd", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, dac2hd_of_dev_ids);

static struct platform_driver dac2hd_platform_drv = {
	.driver = {
		.name		= "hifiberry-dac2hd",
		.owner		= THIS_MODULE,
		.of_match_table	= dac2hd_of_dev_ids,
	},
	.probe  = snd_rpi_hb_dac2hd_probe,
};
module_platform_driver(dac2hd_platform_drv);

MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR("Joerg Schambacher <joerg@i2audio.com>");
MODULE_AUTHOR("Clive Messer <clive.messer@digitaldreamtime.co.uk>");
MODULE_DESCRIPTION("ALTernative ASoC Driver for HiFiBerry DAC2 HD");
MODULE_LICENSE("GPL v2");
