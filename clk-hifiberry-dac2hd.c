// SPDX-License-Identifier: GPL-2.0
/*
 * Clock Driver for HiFiBerry DAC+ HD
 *
 * Author: Joerg Schambacher, i2Audio GmbH for HiFiBerry
 *         Copyright 2020
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

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/regmap.h>

#define DRV_VERSION "5.2.1"

#define CLK_DAC2HD_NO_PLL_RESET		0
#define CLK_DAC2HD_PLL_RESET		1
#define CLK_DAC2HD_PLL_MAX_REGISTER	256
#define CLK_DAC2HD_DEFAULT_RATE		44100

static const struct reg_default clk_hb_dac2hd_pll_reg_soft_reset = {177, 0xAC};

static struct reg_default clk_hb_dac2hd_pll_reg_defaults[] = {
	{0x02, 0x53}, {0x03, 0x00}, {0x07, 0x20}, {0x0F, 0x00},
	{0x10, 0x0D}, {0x11, 0x1D}, {0x12, 0x0D}, {0x13, 0x8C},
	{0x14, 0x8C}, {0x15, 0x8C}, {0x16, 0x8C}, {0x17, 0x8C},
	{0x18, 0x2A}, {0x1C, 0x00}, {0x1D, 0x0F}, {0x1F, 0x00},
	{0x2A, 0x00}, {0x2C, 0x00}, {0x2F, 0x00}, {0x30, 0x00},
	{0x31, 0x00}, {0x32, 0x00}, {0x34, 0x00}, {0x37, 0x00},
	{0x38, 0x00}, {0x39, 0x00}, {0x3A, 0x00}, {0x3B, 0x01},
	{0x3E, 0x00}, {0x3F, 0x00}, {0x40, 0x00}, {0x41, 0x00},
	{0x5A, 0x00}, {0x5B, 0x00}, {0x95, 0x00}, {0x96, 0x00},
	{0x97, 0x00}, {0x98, 0x00}, {0x99, 0x00}, {0x9A, 0x00},
	{0x9B, 0x00}, {0xA2, 0x00}, {0xA3, 0x00}, {0xA4, 0x00},
	{0xB7, 0x92},

	{0x1A, 0x3D}, {0x1B, 0x09}, {0x1E, 0xF3}, {0x20, 0x13},
	{0x21, 0x75}, {0x2B, 0x04}, {0x2D, 0x11}, {0x2E, 0xE0},
	              {0x35, 0x9D}, {0x36, 0x00}, {0x3C, 0x42},
	{0x3D, 0x7A},

//	{177, 0xAC},
};

#ifdef CLK_DAC2HD_STATIC_DEFAULTS
/*
 * DT reg defaults
 */
static struct reg_default clk_hb_dac2hd_common_pll_regs[] = {
	{0x02, 0x53}, {0x03, 0x00}, {0x07, 0x20}, {0x0F, 0x00},
	{0x10, 0x0D}, {0x11, 0x1D}, {0x12, 0x0D}, {0x13, 0x8C},
	{0x14, 0x8C}, {0x15, 0x8C}, {0x16, 0x8C}, {0x17, 0x8C},
	{0x18, 0x2A}, {0x1C, 0x00}, {0x1D, 0x0F}, {0x1F, 0x00},
	{0x2A, 0x00}, {0x2C, 0x00}, {0x2F, 0x00}, {0x30, 0x00},
	{0x31, 0x00}, {0x32, 0x00}, {0x34, 0x00}, {0x37, 0x00},
	{0x38, 0x00}, {0x39, 0x00}, {0x3A, 0x00}, {0x3B, 0x01},
	{0x3E, 0x00}, {0x3F, 0x00}, {0x40, 0x00}, {0x41, 0x00},
	{0x5A, 0x00}, {0x5B, 0x00}, {0x95, 0x00}, {0x96, 0x00},
	{0x97, 0x00}, {0x98, 0x00}, {0x99, 0x00}, {0x9A, 0x00},
	{0x9B, 0x00}, {0xA2, 0x00}, {0xA3, 0x00}, {0xA4, 0x00},
	{0xB7, 0x92},
};
static int clk_hb_dac2hd_num_common_pll_regs =
			ARRAY_SIZE(clk_hb_dac2hd_common_pll_regs);

static struct reg_default clk_hb_dac2hd_dedicated_192k_pll_regs[] = {
	{0x1A, 0x0C}, {0x1B, 0x35}, {0x1E, 0xF0}, {0x20, 0x09},
	{0x21, 0x50}, {0x2B, 0x02}, {0x2D, 0x10}, {0x2E, 0x40},
	{0x33, 0x01}, {0x35, 0x22}, {0x36, 0x80}, {0x3C, 0x22},
	{0x3D, 0x46},
};
static int clk_hb_dac2hd_num_dedicated_192k_pll_regs =
			ARRAY_SIZE(clk_hb_dac2hd_dedicated_192k_pll_regs);

static struct reg_default clk_hb_dac2hd_dedicated_96k_pll_regs[] = {
	{0x1A, 0x0C}, {0x1B, 0x35}, {0x1E, 0xF0}, {0x20, 0x09},
	{0x21, 0x50}, {0x2B, 0x02}, {0x2D, 0x10}, {0x2E, 0x40},
	{0x33, 0x01}, {0x35, 0x47}, {0x36, 0x00}, {0x3C, 0x32},
	{0x3D, 0x46},
};
static int clk_hb_dac2hd_num_dedicated_96k_pll_regs = 
			ARRAY_SIZE(clk_hb_dac2hd_dedicated_96k_pll_regs);

static struct reg_default clk_hb_dac2hd_dedicated_48k_pll_regs[] = {
	{0x1A, 0x0C}, {0x1B, 0x35}, {0x1E, 0xF0}, {0x20, 0x09},
	{0x21, 0x50}, {0x2B, 0x02}, {0x2D, 0x10}, {0x2E, 0x40},
	{0x33, 0x01}, {0x35, 0x90}, {0x36, 0x00}, {0x3C, 0x42},
	{0x3D, 0x46},
};
static int clk_hb_dac2hd_num_dedicated_48k_pll_regs = 
			ARRAY_SIZE(clk_hb_dac2hd_dedicated_48k_pll_regs);

static struct reg_default clk_hb_dac2hd_dedicated_176k4_pll_regs[] = {
	{0x1A, 0x3D}, {0x1B, 0x09}, {0x1E, 0xF3}, {0x20, 0x13},
	{0x21, 0x75}, {0x2B, 0x04}, {0x2D, 0x11}, {0x2E, 0xE0},
	{0x33, 0x02}, {0x35, 0x25}, {0x36, 0xC0}, {0x3C, 0x22},
	{0x3D, 0x7A},
};
static int clk_hb_dac2hd_num_dedicated_176k4_pll_regs =
			ARRAY_SIZE(clk_hb_dac2hd_dedicated_176k4_pll_regs);

static struct reg_default clk_hb_dac2hd_dedicated_88k2_pll_regs[] = {
	{0x1A, 0x3D}, {0x1B, 0x09}, {0x1E, 0xF3}, {0x20, 0x13},
	{0x21, 0x75}, {0x2B, 0x04}, {0x2D, 0x11}, {0x2E, 0xE0},
	{0x33, 0x01}, {0x35, 0x4D}, {0x36, 0x80}, {0x3C, 0x32},
	{0x3D, 0x7A},
};
static int clk_hb_dac2hd_num_dedicated_88k2_pll_regs =
			ARRAY_SIZE(clk_hb_dac2hd_dedicated_88k2_pll_regs);

static struct reg_default clk_hb_dac2hd_dedicated_44k1_pll_regs[] = {
	{0x1A, 0x3D}, {0x1B, 0x09}, {0x1E, 0xF3}, {0x20, 0x13},
	{0x21, 0x75}, {0x2B, 0x04}, {0x2D, 0x11}, {0x2E, 0xE0},
	{0x33, 0x01}, {0x35, 0x9D}, {0x36, 0x00}, {0x3C, 0x42},
	{0x3D, 0x7A},
};
static int clk_hb_dac2hd_num_dedicated_44k1_pll_regs =
			ARRAY_SIZE(clk_hb_dac2hd_dedicated_44k1_pll_regs);
#else
static struct reg_default
	clk_hb_dac2hd_common_pll_regs[CLK_DAC2HD_PLL_MAX_REGISTER];
static int clk_hb_dac2hd_num_common_pll_regs;

static struct reg_default 
	clk_hb_dac2hd_dedicated_192k_pll_regs[CLK_DAC2HD_PLL_MAX_REGISTER];
static int clk_hb_dac2hd_num_dedicated_192k_pll_regs;

static struct reg_default
	clk_hb_dac2hd_dedicated_96k_pll_regs[CLK_DAC2HD_PLL_MAX_REGISTER];
static int clk_hb_dac2hd_num_dedicated_96k_pll_regs;

static struct reg_default
	clk_hb_dac2hd_dedicated_48k_pll_regs[CLK_DAC2HD_PLL_MAX_REGISTER];
static int clk_hb_dac2hd_num_dedicated_48k_pll_regs;

static struct reg_default
	clk_hb_dac2hd_dedicated_176k4_pll_regs[CLK_DAC2HD_PLL_MAX_REGISTER];
static int clk_hb_dac2hd_num_dedicated_176k4_pll_regs;

static struct reg_default
	clk_hb_dac2hd_dedicated_88k2_pll_regs[CLK_DAC2HD_PLL_MAX_REGISTER];
static int clk_hb_dac2hd_num_dedicated_88k2_pll_regs;

static struct reg_default
	clk_hb_dac2hd_dedicated_44k1_pll_regs[CLK_DAC2HD_PLL_MAX_REGISTER];
static int clk_hb_dac2hd_num_dedicated_44k1_pll_regs;
#endif /* CLK_DAC2HD_STATIC_DEFAULTS */

/**
 * struct clk_hb_dac2hd_drvdata - Common struct to the HiFiBerry DAC2 HD Clk
 * @hw: clk_hw for the common clk framework
 */
struct clk_hb_dac2hd_drvdata {
	struct regmap *regmap;
	struct clk *clk;
	struct clk_hw hw;
	unsigned long rate;
	struct device *dev;
#ifdef CLK_DAC2HD_PREPARE_INIT
	bool prepared;
#endif /* CLK_DAC2HD_PREPARE_INIT */
};

#define to_clk_hb_dac2hd(_hw)\
		container_of(_hw, struct clk_hb_dac2hd_drvdata, hw)

static int clk_hb_dac2hd_write_pll_regs(struct device *dev,
					struct regmap *regmap,
					struct reg_default *regs,
					int num, int do_pll_reset)
{
	int i;
	int ret = 0;
//	char pll_soft_reset[] = { 177, 0xAC, };

	dev_dbg(dev, "%s: ENTER: do_pll_reset=%s\n", __func__, 
		(do_pll_reset ? "true" : "false"));

	for (i = 0; i < num; i++) {
		ret |= regmap_write(regmap, regs[i].reg, regs[i].def);
		if (ret) {
			dev_err(dev, "%s: EXIT [%d]: failed to write regmap!\n",
				__func__, ret);
			return ret;
		}
	}
	if (do_pll_reset) {
#ifdef DDEBUG
		dev_dbg(dev, "%s: re-setting pll\n", __func__);
#endif /* DDEBUG */
//		ret |= regmap_write(regmap, pll_soft_reset[0],
//				    pll_soft_reset[1]);
		ret = regmap_write(regmap, clk_hb_dac2hd_pll_reg_soft_reset.reg,
				   clk_hb_dac2hd_pll_reg_soft_reset.def);
		if (ret) {
			dev_err(dev, "%s: EXIT [%d]: failed to write "
				"regmap pll_soft_reset!\n", __func__, ret);
			return ret;
		}
//		mdelay(10);
		usleep_range(9950, 10050);
	}

//	return ret;
	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}
#ifdef CLK_DAC2HD_PREPARE_INIT
static int clk_hb_dac2hd_is_prepared(struct clk_hw *hw)
{
	struct clk_hb_dac2hd_drvdata *drvdata = to_clk_hb_dac2hd(hw);
	struct device *dev = drvdata->dev;
	
	dev_dbg(dev, "%s: ENTER: EXIT [%s]\n", __func__,
		(drvdata->prepared ? "true" : "false"));
	return drvdata->prepared;
}

static int clk_hb_dac2hd_prepare(struct clk_hw *hw)
{
	int ret = 0;
	struct clk_hb_dac2hd_drvdata *drvdata = to_clk_hb_dac2hd(hw);
	struct device *dev = drvdata->dev;

	dev_dbg(dev, "%s: ENTER\n", __func__);
	
	if (drvdata->prepared)
		goto out;
#ifdef DDEBUG
	dev_dbg(dev, "%s: load pll_reg_defaults\n", __func__);
#endif /* DDEBUG */
	ret = clk_hb_dac2hd_write_pll_regs(dev, drvdata->regmap,
				clk_hb_dac2hd_pll_reg_defaults,
				ARRAY_SIZE(clk_hb_dac2hd_pll_reg_defaults),
				CLK_DAC2HD_NO_PLL_RESET);
	if (ret) {
		dev_err(dev, "%s: EXIT [%d]: write_pll_regs(pll_reg_defaults) "
			"failed!\n", __func__, ret);
		return ret;
	}
#ifdef DDEBUG
	dev_dbg(dev, "%s: load common_pll_regs\n", __func__);
#endif /* DDEBUG */
	ret = clk_hb_dac2hd_write_pll_regs(dev, drvdata->regmap,
					   clk_hb_dac2hd_common_pll_regs,
					   clk_hb_dac2hd_num_common_pll_regs,
					   CLK_DAC2HD_NO_PLL_RESET);
	if (ret) {
		dev_err(dev, "%s: EXIT [%d]: write_pll_regs (common_pll_regs) "
			"failed!\n", __func__, ret);
		return ret;
	}

#ifdef DDEBUG
	dev_dbg(dev, "%s: load 44k1_pll_regs\n", __func__);
#endif /* DDEBUG */
	ret = clk_hb_dac2hd_write_pll_regs(dev, drvdata->regmap,
				clk_hb_dac2hd_dedicated_44k1_pll_regs,
				clk_hb_dac2hd_num_dedicated_44k1_pll_regs,
				CLK_DAC2HD_PLL_RESET);
	if (ret) {
		dev_err(dev, "%s: EXIT [%d]: write_pll_regs (44k1_pll_regs) "
			"failed!\n", __func__, ret);
		return ret;
	}
	
	drvdata->rate = CLK_DAC2HD_DEFAULT_RATE;
	drvdata->prepared = true;
out:
	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static void clk_hb_dac2hd_unprepare(struct clk_hw *hw)
{
	struct clk_hb_dac2hd_drvdata *drvdata = to_clk_hb_dac2hd(hw);
	drvdata->prepared = false;
	dev_dbg(drvdata->dev, "%s: ENTER: EXIT [void]\n", __func__);
}
#endif /* CLK_DAC2HD_PREPARE_INIT */
static unsigned long clk_hb_dac2hd_recalc_rate(struct clk_hw *hw,
	unsigned long parent_rate)
{
	struct clk_hb_dac2hd_drvdata *drvdata = to_clk_hb_dac2hd(hw);

	dev_dbg(drvdata->dev, "%s: ENTER: parent_rate=%lu: EXIT [%lu]\n",
		__func__, parent_rate, drvdata->rate);	
	return drvdata->rate;
}

static long clk_hb_dac2hd_round_rate(struct clk_hw *hw,
	unsigned long rate, unsigned long *parent_rate)
{
	struct clk_hb_dac2hd_drvdata *drvdata = to_clk_hb_dac2hd(hw);

	dev_dbg(drvdata->dev, "%s: ENTER: rate=%lu: EXIT [%lu]\n",
		__func__, rate, rate);
	return rate;
}

static int clk_hb_dac2hd_set_rate(struct clk_hw *hw,
	unsigned long rate, unsigned long parent_rate)
{
	int ret;
	struct clk_hb_dac2hd_drvdata *drvdata = to_clk_hb_dac2hd(hw);
	struct device *dev = drvdata->dev;

	dev_dbg(dev, "%s: ENTER: rate=%lu, parent_rate=%lu\n", __func__, rate,
		parent_rate);

#ifdef CLK_DAC2HD_PREPARE_INIT
	if (!drvdata->prepared) {
#ifdef DDEBUG
		dev_dbg(dev, "%s: prepare clock\n", __func__);
#endif /* DDEBUG */
		ret = clk_hb_dac2hd_prepare(hw);
		if (ret) {
			dev_err(dev, "%s: EXIT [%d]: prepare failed!\n",
				__func__, ret);
			return ret;
		}
	}
#endif /* CLK_DAC2HD_PREPARE_INIT */

	if (rate == drvdata->rate) {
		dev_dbg(dev, "%s: EXIT [0]: noop - already running at %lu\n",
			__func__, rate);
		return 0;
	}

	switch (rate) {
	case 44100:
#ifdef DDEBUG
		dev_dbg(dev, "%s: loading 44k1_pll_regs\n", __func__);
#endif /* DDEBUG */
		ret = clk_hb_dac2hd_write_pll_regs(drvdata->dev,
			drvdata->regmap,
			clk_hb_dac2hd_dedicated_44k1_pll_regs,
			clk_hb_dac2hd_num_dedicated_44k1_pll_regs,
			CLK_DAC2HD_PLL_RESET);
		break;
	case 88200:
#ifdef DDEBUG
		dev_dbg(dev, "%s: loading 88k2_pll_regs\n", __func__);
#endif /* DDEBUG */
		ret = clk_hb_dac2hd_write_pll_regs(drvdata->dev,
			drvdata->regmap,
			clk_hb_dac2hd_dedicated_88k2_pll_regs,
			clk_hb_dac2hd_num_dedicated_88k2_pll_regs,
			CLK_DAC2HD_PLL_RESET);
		break;
	case 176400:
#ifdef DDEBUG
		dev_dbg(dev, "%s: loading 176k4_pll_regs\n", __func__);
#endif /* DDEBUG */
		ret = clk_hb_dac2hd_write_pll_regs(drvdata->dev,
			drvdata->regmap,
			clk_hb_dac2hd_dedicated_176k4_pll_regs,
			clk_hb_dac2hd_num_dedicated_176k4_pll_regs,
			CLK_DAC2HD_PLL_RESET);
		break;
	case 48000:
#ifdef DDEBUG
		dev_dbg(dev, "%s: loading 48k_pll_regs\n", __func__);
#endif /* DDEBUG */
		ret = clk_hb_dac2hd_write_pll_regs(drvdata->dev,
			drvdata->regmap,
			clk_hb_dac2hd_dedicated_48k_pll_regs,
			clk_hb_dac2hd_num_dedicated_48k_pll_regs,
			CLK_DAC2HD_PLL_RESET);
		break;
	case 96000:
#ifdef DDEBUG
		dev_dbg(dev, "%s: loading 96k_pll_regs\n", __func__);
#endif /* DDEBUG */
		ret = clk_hb_dac2hd_write_pll_regs(drvdata->dev,
			drvdata->regmap,
			clk_hb_dac2hd_dedicated_96k_pll_regs,
			clk_hb_dac2hd_num_dedicated_96k_pll_regs,
			CLK_DAC2HD_PLL_RESET);
		break;
	case 192000:
#ifdef DDEBUG
		dev_dbg(dev, "%s: loading dedicated_192k_pll_regs\n", __func__);
#endif /* DDEBUG */
		ret = clk_hb_dac2hd_write_pll_regs(drvdata->dev,
			drvdata->regmap,
			clk_hb_dac2hd_dedicated_192k_pll_regs,
			clk_hb_dac2hd_num_dedicated_192k_pll_regs,
			CLK_DAC2HD_PLL_RESET);
		break;
	default:
//		ret = -EINVAL;
		dev_err(dev, "%s: EXIT [-EINVAL]: invalid rate=%lu!\n",
			__func__, rate);
		return -EINVAL;		
//		break;
	}

	if (ret < 0) {
		dev_err(dev, "%s: EXIT [%d]: error writing pll "
			"registers for rate=%lu!\n", __func__, ret, rate);
		return ret;
	}

//	to_clk_hb_dac2hd(hw)->rate = rate;
	drvdata->rate = rate;

	dev_dbg(dev, "%s: EXIT [%d]\n", __func__, ret);
	return ret;
}
#ifndef CLK_DAC2HD_STATIC_DEFAULTS
static int clk_hb_dac2hd_get_prop_values(struct device *dev, char *prop_name,
					 struct reg_default *regs)
{
	int ret;
	int i;
	u8 tmp[2 * CLK_DAC2HD_PLL_MAX_REGISTER];

	dev_dbg(dev, "%s: ENTER: prop_name=%s\n", __func__, prop_name);

	ret = of_property_read_variable_u8_array(dev->of_node, prop_name,
			tmp, 0, 2 * CLK_DAC2HD_PLL_MAX_REGISTER);
	if (ret < 0) {
		dev_err(dev, "%s: EXIT [%d]: read_variable_u8_array(%s) "
			"returns: [%d]\n", __func__, ret, prop_name, ret);
		return ret;
	}
	if (ret & 1) {
		dev_err(dev, "%s: EXIT [-EINVAL]: <%s> -> #%i odd number of "
			"bytes for reg/val pairs!", __func__, prop_name, ret);
		return -EINVAL;
	}

	ret /= 2;
	for (i = 0; i < ret; i++) {
		regs[i].reg = (u32)tmp[2 * i];
		regs[i].def = (u32)tmp[2 * i + 1];
	}

	dev_dbg(dev, "%s: EXIT [%d]\n", __func__, ret);
	return ret;
}

static int clk_hb_dac2hd_dt_parse(struct device *dev)
{
	dev_dbg(dev, "%s: ENTER\n", __func__);

	clk_hb_dac2hd_num_common_pll_regs =
		clk_hb_dac2hd_get_prop_values(dev, "common_pll_regs",
				clk_hb_dac2hd_common_pll_regs);
	clk_hb_dac2hd_num_dedicated_44k1_pll_regs =
		clk_hb_dac2hd_get_prop_values(dev, "44k1_pll_regs",
				clk_hb_dac2hd_dedicated_44k1_pll_regs);
	clk_hb_dac2hd_num_dedicated_88k2_pll_regs =
		clk_hb_dac2hd_get_prop_values(dev, "88k2_pll_regs",
				clk_hb_dac2hd_dedicated_88k2_pll_regs);
	clk_hb_dac2hd_num_dedicated_176k4_pll_regs =
		clk_hb_dac2hd_get_prop_values(dev, "176k4_pll_regs",
				clk_hb_dac2hd_dedicated_176k4_pll_regs);
	clk_hb_dac2hd_num_dedicated_48k_pll_regs =
		clk_hb_dac2hd_get_prop_values(dev, "48k_pll_regs",
				clk_hb_dac2hd_dedicated_48k_pll_regs);
	clk_hb_dac2hd_num_dedicated_96k_pll_regs =
		clk_hb_dac2hd_get_prop_values(dev, "96k_pll_regs",
				clk_hb_dac2hd_dedicated_96k_pll_regs);
	clk_hb_dac2hd_num_dedicated_192k_pll_regs =
		clk_hb_dac2hd_get_prop_values(dev, "192k_pll_regs",
				clk_hb_dac2hd_dedicated_192k_pll_regs);

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}
#endif /* CLK_DAC2HD_STATIC_DEFAULTS */
const struct clk_ops clk_hb_dac2hd_clk_ops = {
	.recalc_rate = clk_hb_dac2hd_recalc_rate,
	.round_rate  = clk_hb_dac2hd_round_rate,
	.set_rate    = clk_hb_dac2hd_set_rate,
#ifdef CLK_DAC2HD_PREPARE_INIT
	.prepare     = clk_hb_dac2hd_prepare,
	.unprepare   = clk_hb_dac2hd_unprepare,
	.is_prepared = clk_hb_dac2hd_is_prepared,
#endif /* CLK_DAC2HD_PREPARE_INIT */
};

const struct regmap_config clk_hb_dac2hd_pll_regmap_cfg = {
	.reg_bits         = 8,
	.val_bits         = 8,
	.max_register     = CLK_DAC2HD_PLL_MAX_REGISTER,
	.reg_defaults     = clk_hb_dac2hd_pll_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(clk_hb_dac2hd_pll_reg_defaults),
	.cache_type       = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_GPL(clk_hb_dac2hd_pll_regmap_cfg);

static int clk_hb_dac2hd_i2c_probe(struct i2c_client *i2c,
				   const struct i2c_device_id *id)
{
	struct clk_hb_dac2hd_drvdata *drvdata;
	int ret = 0;
	struct clk_init_data init;
	struct device *dev = &i2c->dev;
	struct device_node *dev_node = dev->of_node;
	struct regmap_config config = clk_hb_dac2hd_pll_regmap_cfg;

	dev_dbg(dev, "%s: ENTER\n", __func__);

	drvdata = devm_kzalloc(&i2c->dev, sizeof(struct clk_hb_dac2hd_drvdata),
			       GFP_KERNEL);
	if (!drvdata) {
		dev_err(dev, "%s: EXIT [-ENOMEM]: devm_kzalloc drvdata "
			"failed!\n", __func__);
		return -ENOMEM;
	}

	i2c_set_clientdata(i2c, drvdata);

	drvdata->regmap = devm_regmap_init_i2c(i2c, &config);

	if (IS_ERR(drvdata->regmap)) {
		ret = PTR_ERR(drvdata->regmap);
		dev_err(dev, "%s: EXIT [%d]: devm_regmap_init_i2c failed!\n",
			__func__, ret);
		return ret;
	}

#ifndef CLK_DAC2HD_STATIC_DEFAULTS
	/* populate reg_defaults configs from DT */
	clk_hb_dac2hd_dt_parse(dev);
#endif /* CLK_DAC2HD_STATIC_DEFAULTS */

#ifndef CLK_DAC2HD_PREPARE_INIT
	/* start PLL to allow detection of DAC */
#ifdef DDEBUG
	dev_dbg(dev, "%s: load pll_reg_defaults\n",
		__func__);
#endif /* DDEBUG */
	ret = clk_hb_dac2hd_write_pll_regs(dev, drvdata->regmap,
				clk_hb_dac2hd_pll_reg_defaults,
				ARRAY_SIZE(clk_hb_dac2hd_pll_reg_defaults),
				CLK_DAC2HD_PLL_RESET);
	if (ret) {
		dev_err(dev, "%s: EXIT [%d]: write_pll_regs(pll_reg_defaults) "
			"failed!\n", __func__, ret);
		return ret;
	}

	/* restart PLL with configs from DTB */
#ifdef DDEBUG
	dev_dbg(dev, "%s: load common_pll_regs\n", __func__);
#endif /* DDEBUG */
	ret = clk_hb_dac2hd_write_pll_regs(dev, drvdata->regmap,
					   clk_hb_dac2hd_common_pll_regs,
					   clk_hb_dac2hd_num_common_pll_regs,
					   CLK_DAC2HD_NO_PLL_RESET);
	if (ret) {
		dev_err(dev, "%s: EXIT [%d]: write_pll_regs(common_pll_regs) "
			"failed!\n", __func__, ret);
		return ret;
	}
#endif /* CLK_DAC2HD_PREPARE_INIT */

	init.name = "clk-hifiberry-dac2hd";
	init.ops = &clk_hb_dac2hd_clk_ops;
	init.flags = 0;
	init.parent_names = NULL;
	init.num_parents = 0;

	drvdata->hw.init = &init;
	drvdata->dev = dev;

#ifdef DDEBUG
	dev_dbg(dev, "%s: register clk\n", __func__);
#endif /* DDEBUG */
	drvdata->clk = devm_clk_register(dev, &drvdata->hw);
	if (IS_ERR(drvdata->clk)) {
		ret = PTR_ERR(drvdata->clk);
		dev_err(dev, "%s: EXIT [%d]: devm_clk_register failed!\n",
			__func__, ret);
		return ret;
	}

#ifdef DDEBUG
	dev_dbg(dev, "%s: register clk provider for node\n", __func__);
#endif /* DDEBUG */
	ret = of_clk_add_provider(dev_node, of_clk_src_simple_get,
				  drvdata->clk);
	if (ret != 0) {
		dev_err(dev, "%s: EXIT [%d]: of_clk_add_provider failed!\n",
			__func__, ret);
		return ret;
	}

#ifndef CLK_DAC2HD_PREPARE_INIT
#ifdef DDEBUG
	dev_dbg(dev, "%s: clk_set_rate(%d)\n", __func__,
		CLK_DAC2HD_DEFAULT_RATE);
#endif /* DDEBUG */
	ret = clk_set_rate(drvdata->hw.clk, CLK_DAC2HD_DEFAULT_RATE);
	if (ret != 0) {
		dev_err(dev, "%s: EXIT [-EINVAL]: clk_set_rate(%d) "
			"returns: [%d]\n", __func__, CLK_DAC2HD_DEFAULT_RATE,
			ret);
		return -EINVAL;
	}
#endif /* CLK_DAC2HD_PREPARE_INIT */

	dev_dbg(dev, "%s: EXIT [%d]\n", __func__, ret);

	return ret;
}

static int clk_hb_dac2hd_remove(struct device *dev)
{
	dev_dbg(dev, "%s: ENTER\n", __func__);
	of_clk_del_provider(dev->of_node);
	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static int clk_hb_dac2hd_i2c_remove(struct i2c_client *i2c)
{
	dev_dbg(&i2c->dev, "%s: ENTER\n", __func__);
	clk_hb_dac2hd_remove(&i2c->dev);
	dev_dbg(&i2c->dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static const struct i2c_device_id clk_hb_dac2hd_i2c_dev_ids[] = {
	{ "dac2hd-clk", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, clk_hb_dac2hd_i2c_dev_ids);

static const struct of_device_id clk_hb_dac2hd_of_dev_ids[] = {
	{ .compatible = "hifiberry,dac2hd-clk", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, clk_hb_dac2hd_of_dev_ids);

static struct i2c_driver clk_hb_dac2hd_i2c_drv = {
	.probe    = clk_hb_dac2hd_i2c_probe,
	.remove   = clk_hb_dac2hd_i2c_remove,
	.id_table = clk_hb_dac2hd_i2c_dev_ids,
	.driver   = {
		.name           = "dac2hd-clk",
		.of_match_table = of_match_ptr(clk_hb_dac2hd_of_dev_ids),
	},
};
module_i2c_driver(clk_hb_dac2hd_i2c_drv);

MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("ALTernative HiFiBerry DAC2 HD clock driver");
MODULE_AUTHOR("Joerg Schambacher <joerg@i2audio.com>");
MODULE_AUTHOR("Clive Messer <clive.messer@digitaldreamtime.co.uk>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:clk-hifiberry-dac2hd");
