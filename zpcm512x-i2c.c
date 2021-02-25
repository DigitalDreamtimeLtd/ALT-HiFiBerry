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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/acpi.h>

#include "zpcm512x.h"

#define DRV_VERSION "4.0.0"

static int zpcm512x_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	int ret = 0;
	struct device *dev = &client->dev;
	struct regmap_config config = zpcm512x_regmap;
	struct regmap *regmap;

	dev_dbg(dev, "%s: ENTER\n", __func__);

	/* msb needs to be set to enable auto-increment of addresses */
	config.read_flag_mask = 0x80;
	config.write_flag_mask = 0x80;

	regmap = devm_regmap_init_i2c(client, &config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(dev, "%s: EXIT [%d]: regmap_init_i2c failed!\n",
			__func__, ret);
		return ret;;
	}

	ret = zpcm512x_probe(dev, regmap);

	if (ret < 0)
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "%s: EXIT [%d]\n", __func__, ret);
		else
			dev_info(dev, "%s: EXIT [-EPROBE_DEFER]\n", __func__);
	else
		dev_dbg(dev, "%s: EXIT [%d]\n", __func__, ret);

	return ret;
}

static int zpcm512x_i2c_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;

	dev_dbg(dev, "%s: ENTER\n", __func__);

	zpcm512x_remove(dev);

	dev_dbg(dev, "%s: EXIT [0]\n", __func__);
	return 0;
}

static const struct i2c_device_id zpcm512x_i2c_dev_ids[] = {
	{ "zpcm5121", },
	{ "zpcm5122", },
	{ "zpcm5141", },
	{ "zpcm5142", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, zpcm512x_i2c_dev_ids);

#if defined(CONFIG_OF)
static const struct of_device_id zpcm512x_of_dev_ids[] = {
	{ .compatible = "ti,zpcm5121", },
	{ .compatible = "ti,zpcm5122", },
	{ .compatible = "ti,zpcm5141", },
	{ .compatible = "ti,zpcm5142", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, zpcm512x_of_dev_ids);
#endif /* CONFIG_OF */

#ifdef CONFIG_ACPI
static const struct acpi_device_id zpcm512x_acpi_dev_ids[] = {
	{ "104C5121", 0 },
	{ "104C5122", 0 },
	{ "104C5141", 0 },
	{ "104C5142", 0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(acpi, zpcm512x_acpi_dev_ids);
#endif

static struct i2c_driver zpcm512x_i2c_drv = {
	.probe    = zpcm512x_i2c_probe,
	.remove   = zpcm512x_i2c_remove,
	.id_table = zpcm512x_i2c_dev_ids,
	.driver	  = {
		.name             = "zpcm512x",
		.of_match_table   = of_match_ptr(zpcm512x_of_dev_ids),
		.acpi_match_table = ACPI_PTR(zpcm512x_acpi_dev_ids),
		.pm               = &zpcm512x_pm_ops,
	},
};
module_i2c_driver(zpcm512x_i2c_drv);

MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("ALTernative ASoC PCM512x codec driver - I2C");
MODULE_AUTHOR("Mark Brown <broonie@kernel.org>");
MODULE_AUTHOR("Clive Messer <clive.messer@digitaldreamtime.org.uk>");
MODULE_LICENSE("GPL v2");
