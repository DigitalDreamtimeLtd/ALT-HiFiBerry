// SPDX-License-Identifier: GPL-2.0
/*
 * Clock Driver for HiFiBerry DAC+ Pro
 *
 * Author: Stuart MacLean
 *         Copyright 2015
 *
 * Author: Clive Messer <clive.messer@digitaldreamtime.co.uk>
 *         Copyright (c) Digital Dreamtime Ltd 2016-2020
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
#include <linux/clkdev.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#define DRV_VERSION "4.0.0"

/* Clock rate of CLK44EN attached to GPIO6 pin */
#define CLK_44EN_RATE 22579200UL
/* Clock rate of CLK48EN attached to GPIO3 pin */
#define CLK_48EN_RATE 24576000UL

/**
 * struct clk_hb_dacpro_drvdata - Common struct to the HiFiBerry DAC+ Pro
 * @hw: clk_hw for the common clk framework
 * @mode: 0 => CLK44EN, 1 => CLK48EN
 */
struct clk_hb_dacpro_drvdata {
	struct clk_hw hw;
	uint8_t mode;
	struct device *dev;
};

#define to_clk_hb_dacpro(_hw)\
		 container_of(_hw, struct clk_hb_dacpro_drvdata, hw)

static unsigned long clk_hb_dacpluspro_recalc_rate(struct clk_hw *hw,
						   unsigned long parent_rate)
{
	struct clk_hb_dacpro_drvdata *clk = to_clk_hb_dacpro(hw);
	unsigned long rate = (clk->mode == 0) ? CLK_44EN_RATE : CLK_48EN_RATE;

	dev_dbg(clk->dev, "%s: ENTER: parent_rate=%lu: EXIT [%lu]\n",
		__func__, parent_rate, rate);
	return rate;
}

static long clk_hb_dacpluspro_round_rate(struct clk_hw *hw,
	unsigned long rate, unsigned long *parent_rate)
{
	long actual_rate;
	struct clk_hb_dacpro_drvdata *clk = to_clk_hb_dacpro(hw);

	dev_dbg(clk->dev, "%s: ENTER: rate=%lu\n", __func__, rate);

	if (rate <= CLK_44EN_RATE) {
		actual_rate = (long)CLK_44EN_RATE;
	} else if (rate >= CLK_48EN_RATE) {
		actual_rate = (long)CLK_48EN_RATE;
	} else {
		long diff44Rate = (long)(rate - CLK_44EN_RATE);
		long diff48Rate = (long)(CLK_48EN_RATE - rate);

		if (diff44Rate < diff48Rate)
			actual_rate = (long)CLK_44EN_RATE;
		else
			actual_rate = (long)CLK_48EN_RATE;
	}

	dev_dbg(clk->dev, "%s: EXIT [%ld]\n", __func__, actual_rate);
	return actual_rate;
}

static int clk_hb_dacpluspro_set_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long parent_rate)
{
	unsigned long actual_rate;
	struct clk_hb_dacpro_drvdata *clk = to_clk_hb_dacpro(hw);

	dev_dbg(clk->dev, "%s: ENTER: rate=%lu, parent_rate=%lu\n",
		__func__, rate, parent_rate);

	actual_rate = (unsigned long)clk_hb_dacpluspro_round_rate(hw, rate,
								&parent_rate);
	clk->mode = (actual_rate == CLK_44EN_RATE) ? 0 : 1;

	dev_dbg(clk->dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

const struct clk_ops clk_hb_dacpluspro_rate_ops = {
	.recalc_rate = clk_hb_dacpluspro_recalc_rate,
	.round_rate = clk_hb_dacpluspro_round_rate,
	.set_rate = clk_hb_dacpluspro_set_rate,
};

static int clk_hb_dacpluspro_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct clk_hb_dacpro_drvdata *proclk;
	struct clk *clk;
	struct device *dev;
	struct clk_init_data init;

	dev = &pdev->dev;

	dev_dbg(dev, "%s: ENTER\n", __func__);

	proclk = kzalloc(sizeof(struct clk_hb_dacpro_drvdata), GFP_KERNEL);
	if (!proclk) {
		dev_err(dev, "%s: EXIT [-ENOMEM]: kzalloc "
			"clk_hb_dacpro_drvdata failed!\n", __func__);
		return -ENOMEM;
	}

	init.name = "clk-hifiberry-dacpluspro";
	init.ops = &clk_hb_dacpluspro_rate_ops;
	init.flags = 0;
	init.parent_names = NULL;
	init.num_parents = 0;

	proclk->mode = 1;
	proclk->hw.init = &init;
	proclk->dev = dev;

	clk = devm_clk_register(dev, &proclk->hw);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_err(dev, "%s: EXIT [%d]: failed to register clock "
			"driver!\n", __func__, ret);
		kfree(proclk);
		return ret;
	}

	ret = of_clk_add_provider(dev->of_node, of_clk_src_simple_get,
				  clk);
	if (ret < 0) {
		if (ret == -EPROBE_DEFER)
			dev_info(dev, "%s: EXIT [-EPROBE_DEFER]\n", __func__);
		else
			dev_err(dev, "%s: EXIT [%d]\n", __func__, ret);
	} else
		dev_dbg(dev, "%s: EXIT [%d]\n", __func__, ret);

	return ret;
}

static int clk_hb_dacpluspro_remove(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "%s: ENTER\n", __func__);
	of_clk_del_provider(pdev->dev.of_node);
	dev_dbg(&pdev->dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static const struct of_device_id clk_hb_dacpluspro_of_dev_ids[] = {
	{ .compatible = "hifiberry,dacpluspro-clk",},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, clk_hb_dacpluspro_of_dev_ids);

static struct platform_driver clk_hb_dacpluspro_platform_drv = {
	.probe = clk_hb_dacpluspro_probe,
	.remove = clk_hb_dacpluspro_remove,
	.driver = {
		.name = "clk-hifiberry-dacpluspro",
		.of_match_table = clk_hb_dacpluspro_of_dev_ids,
	},
};

static int __init clk_hb_dacpluspro_init(void)
{
	return platform_driver_register(&clk_hb_dacpluspro_platform_drv);
}
core_initcall(clk_hb_dacpluspro_init);

static void __exit clk_hb_dacpluspro_exit(void)
{
	platform_driver_unregister(&clk_hb_dacpluspro_platform_drv);
}
module_exit(clk_hb_dacpluspro_exit);

MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("ALTernative HiFiBerry DAC+ Pro clock driver");
MODULE_AUTHOR("Stuart MacLean");
MODULE_AUTHOR("Clive Messer <clive.messer@digitaldreamtime.co.uk>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:clk-hifiberry-dacpluspro");
