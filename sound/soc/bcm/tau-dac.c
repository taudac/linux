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

#include <sound/core.h>
#include <sound/soc.h>

#include <linux/i2c.h>

#include "../codecs/wm8741.h"

/*
 * asoc codecs
 */
static struct i2c_board_info wm8741_i2c_devices[] = {
	{
		I2C_BOARD_INFO("wm8741", 0x1a),
//		 .platform_data = &wm8741_pdata_left,
	},
	{
		I2C_BOARD_INFO("wm8741", 0x1b),
//		.platform_data = &wm8741_pdata_right,
	},
};

struct i2c_client *wm8741_i2c_clients[ARRAY_SIZE(wm8741_i2c_devices)];

static struct snd_soc_dai_link_component snd_rpi_tau_dac_codecs[] = {
	{
		.name     = "wm8741.1-001a",
		.dai_name = "wm8741",
	},
	{
		.name     = "wm8741.1-001b",
		.dai_name = "wm8741",
	},
};

/*
 * asoc digital audio interface
 */
static int snd_rpi_tau_dac_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret = 0;
	return ret;
}

static int snd_rpi_tau_dac_startup(struct snd_pcm_substream *substream)
{
	int ret = 0;
	return ret;
}

static int snd_rpi_tau_dac_hw_params(struct snd_pcm_substream *substream,
	                                 struct snd_pcm_hw_params *params)
{
	int ret = 0;
	return ret;
}

static void snd_rpi_tau_dac_shutdown(struct snd_pcm_substream *substream)
{

}

static struct snd_soc_ops snd_rpi_tau_dac_ops = {
	.startup   = snd_rpi_tau_dac_startup,
	.shutdown  = snd_rpi_tau_dac_shutdown,
	.hw_params = snd_rpi_tau_dac_hw_params,
};

static struct snd_soc_dai_link snd_rpi_tau_dac_dai[] = {
	{
		.name          = "TauDAC",
		.stream_name   = "TauDAC HiFi",
		.cpu_dai_name  = "bcm2708-i2s.0",
		.platform_name = "bcm2708-i2s.0",
		.codecs        = snd_rpi_tau_dac_codecs,
		.num_codecs    = ARRAY_SIZE(snd_rpi_tau_dac_codecs),
		.dai_fmt       = SND_SOC_DAIFMT_I2S |
		                 SND_SOC_DAIFMT_NB_NF |
		                 SND_SOC_DAIFMT_CBS_CFS,
		.playback_only = true,
		.ops  = &snd_rpi_tau_dac_ops,
		.init = snd_rpi_tau_dac_init,
	},
};

/*
 * asoc machine driver
 */
static struct snd_soc_card snd_rpi_tau_dac = {
	.name       = "snd_rpi_tau_dac",
	.dai_link   = snd_rpi_tau_dac_dai,
	.num_links  = ARRAY_SIZE(snd_rpi_tau_dac_dai),
};

/*
 * platform device driver
 */
static int snd_rpi_tau_dac_probe(struct platform_device *pdev)
{
	int ret, i;

	snd_rpi_tau_dac.dev = &pdev->dev;

	if (pdev->dev.of_node) {
	    struct device_node *i2s_node;
	    struct device_node *i2c_nodes[ARRAY_SIZE(snd_rpi_tau_dac_codecs)];
	    struct snd_soc_dai_link *dai = &snd_rpi_tau_dac_dai[0];

	    i2s_node = of_parse_phandle(pdev->dev.of_node, "i2s-controller", 0);

	    if (i2s_node != NULL) {
			dai->cpu_dai_name = NULL;
			dai->cpu_of_node = i2s_node;
			dai->platform_name = NULL;
			dai->platform_of_node = i2s_node;
	    }

	    for (i = 0; i < ARRAY_SIZE(i2c_nodes); i++) {
	    	i2c_nodes[i] = of_parse_phandle(pdev->dev.of_node, "codecs", i);

	    	if (i2c_nodes[i] != NULL) {
		    	dai->codecs[i].name = NULL;
		    	dai->codecs[i].of_node = i2c_nodes[i];
		    }
	    }
	}

	ret = snd_soc_register_card(&snd_rpi_tau_dac);

	if (ret != 0)
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n", ret);

	return ret;
}

static int snd_rpi_tau_dac_remove(struct platform_device *pdev)
{
	return snd_soc_unregister_card(&snd_rpi_tau_dac);
}

static const struct of_device_id snd_rpi_tau_dac_of_match[] = {
	{ .compatible = "singularity-audio,tau-dac", },
	{},
};
MODULE_DEVICE_TABLE(of, snd_rpi_tau_dac_of_match);

static struct platform_driver snd_rpi_tau_dac_driver = {
	.driver = {
		.name  = "snd-tau-dac",
		.owner = THIS_MODULE,
		.of_match_table = snd_rpi_tau_dac_of_match,
	},
	.probe  = snd_rpi_tau_dac_probe,
	.remove = snd_rpi_tau_dac_remove,
};

module_platform_driver(snd_rpi_tau_dac_driver);

MODULE_AUTHOR("Sergej Sawazki <taudac@gmx.de>");
MODULE_DESCRIPTION("ASoC Driver for TauDAC");
MODULE_LICENSE("GPL v2");
