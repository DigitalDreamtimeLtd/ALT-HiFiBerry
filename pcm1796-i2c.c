// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PCM1796 ASoC I2C driver
 *
 * Author: Jacob Siverskog <jacob@teenage.engineering>
 *         Copyright (c) Teenage Engineering AB 2016
 *
 * Author: Clive Messer <clive.messer@digitaldreamtime.co.uk>
 *         Copyright (c) Digital Dreamtime Ltd 2020-2021
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/regmap.h>

#include "pcm1796.h"

#define DRV_VERSION "5.2.1"

static int pcm1796_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	int ret = 0;
	struct device *dev = &client->dev;
	struct regmap *regmap;

	dev_dbg(&client->dev, "%s: ENTER\n", __func__);

	regmap = devm_regmap_init_i2c(client, &pcm1796_regmap_cfg);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(dev, "%s: EXIT [%d]: regmap_init_i2c failed!\n",
			__func__, ret);
		return ret;
	}

	ret = pcm1796_probe(dev, regmap);

	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "%s: EXIT [%d]\n", __func__, ret);
		else
			dev_info(dev, "%s: EXIT [-EPROBE_DEFER]\n", __func__);
	} else 
		dev_dbg(dev, "%s: EXIT [%d]\n", __func__, ret);

	return ret;
}

static int pcm1796_i2c_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	dev_dbg(dev, "%s: ENTER\n", __func__);
	pcm1796_remove(dev);
	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static const struct i2c_device_id pcm1796_i2c_dev_ids[] = {
	{ "pcm1796" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, pcm1796_i2c_dev_ids);

#ifdef CONFIG_OF
static const struct of_device_id pcm1796_i2c_of_dev_ids[] = {
	{ .compatible = "ti,pcm1796", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pcm1796_i2c_of_dev_ids);
#endif /* CONFIG_OF */

static struct i2c_driver pcm1796_i2c_drv = {
	.driver   = {
		.name           = "pcm1796",
		.of_match_table = of_match_ptr(pcm1796_i2c_of_dev_ids),
	},
	.id_table = pcm1796_i2c_dev_ids,
	.probe    = pcm1796_i2c_probe,
	.remove   = pcm1796_i2c_remove,
};
module_i2c_driver(pcm1796_i2c_drv);

MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("ALTernative ASoC PCM1796 codec driver - I2C");
MODULE_AUTHOR("Jacob Siverskog <jacob@teenage.engineering>");
MODULE_AUTHOR("Clive Messer <clive.messer@digitaldreamtime.co.uk>");
MODULE_LICENSE("GPL");
