/*
 * ASoC Driver for TauDAC
 *
 * Author:	Sergej Sawazki <taudac@gmx.de>
 *		Copyright 2015
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

#include "../codecs/wm8741.h"

static int snd_rpi_tau_dac_probe(struct platform_device *pdev)
{
	int ret = 0;

	return ret;
}

static int snd_rpi_tau_dac_remove(struct platform_device *pdev)
{
	int ret = 0;

	return ret;
}

static struct platform_driver snd_rpi_tau_dac_driver = {
	.driver = {
		.name   = "snd-tau-dac",
		.owner  = THIS_MODULE,
	},
	.probe  = snd_rpi_tau_dac_probe,
	.remove = snd_rpi_tau_dac_remove,
};

module_platform_driver(snd_rpi_tau_dac_driver);

MODULE_AUTHOR("Sergej Sawazki <ce3a@gmx.de>");
MODULE_DESCRIPTION("ASoC Driver for TauDAC");
MODULE_LICENSE("GPL v2");
