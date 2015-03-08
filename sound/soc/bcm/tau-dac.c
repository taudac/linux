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
#include <sound/pcm_params.h>

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/clk.h>

#include "../codecs/wm8741.h"


#define TAU_DAC_GPIO_MCLK_ENABLE 27
#define TAU_DAC_GPIO_MCLK_SELECT 22


static struct gpio tau_dac_gpios[] = {
	{TAU_DAC_GPIO_MCLK_ENABLE, GPIOF_OUT_INIT_LOW, "TauDAC MCLK Enable Pin"},
	{TAU_DAC_GPIO_MCLK_SELECT, GPIOF_OUT_INIT_LOW, "TauDAC MCLK Select Pin"},
};


/*
 * clocks
 */
struct tau_dac_clks {
	struct clk* ch;
	char name[16];
};

static struct tau_dac_clks tau_dac_lrclks[] = {
	{NULL, "lrclk-cpu"},
	{NULL, "lrclk-dacl"},
	{NULL, "lrclk-dacr"},
};

static struct tau_dac_clks tau_dac_bclks[] = {
	{NULL, "bclk-cpu"},
	{NULL, "bclk-dacl"},
	{NULL, "bclk-dacr"},
};

/*
 * asoc codecs
 */
static struct snd_soc_dai_link_component tau_dac_codecs[] = {
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
static int tau_dac_init(struct snd_soc_pcm_runtime *rtd)
{
	return 0;
}

static int tau_dac_startup(struct snd_pcm_substream *substream)
{
	// TODO: check rate constraints
	return 0;
}

static void tau_dac_shutdown(struct snd_pcm_substream *substream)
{
	// TODO: disable mclk
}

static int tau_dac_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	int ret, i;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai **codec_dais = rtd->codec_dais;
	int num_codecs = rtd->num_codecs;

	unsigned int mclk_freq;	
	unsigned int rate = params_rate(params);
	int width = params_width(params);
	
	switch (rate) {
	case 44100:
	case 88200:
	case 176400: // TODO: check these rates in wm8741.c 168
		mclk_freq = 22579200;
		break;
	case 32000:
	case 48000:
	case 96000:
	case 192000:
		mclk_freq = 24576000;
		break;
	default:
		return -EINVAL;
	}
	
	snd_soc_dai_set_bclk_ratio(cpu_dai, 32 * 2);

	for (i = 0; i < num_codecs; i++) {
		/* set codecs sysclk */
		ret = snd_soc_dai_set_sysclk(codec_dais[i],
			WM8741_SYSCLK, mclk_freq, SND_SOC_CLOCK_IN);
		if (ret != 0) 
			 return ret;
	}
	
	/* set cpu DAI configuration, using external bclk */
	ret = snd_soc_dai_set_fmt(cpu_dai,
		SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret != 0)
		return ret;
	
	/* set codec DAI configuration */
	for (i = 0; i < num_codecs; i++) {
		ret = snd_soc_dai_set_fmt(codec_dais[i],
			SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
		if (ret != 0)
			return ret;
	}

	// TODO: enable mclk, lrclk, bclk
	return ret;
}

static struct snd_soc_ops tau_dac_ops = {
	.startup   = tau_dac_startup,
	.shutdown  = tau_dac_shutdown,
	.hw_params = tau_dac_hw_params,
};

static struct snd_soc_dai_link tau_dac_dai[] = {
	{
		.name          = "TauDAC",
		.stream_name   = "TauDAC HiFi",
		.cpu_dai_name  = "bcm2708-i2s.0",
		.platform_name = "bcm2708-i2s.0",
		.codecs        = tau_dac_codecs,
		.num_codecs    = ARRAY_SIZE(tau_dac_codecs),
		.dai_fmt       = SND_SOC_DAIFMT_I2S |
		                 SND_SOC_DAIFMT_NB_NF |
		                 SND_SOC_DAIFMT_CBS_CFS,
		.playback_only = true,
		.ops  = &tau_dac_ops,
		.init = tau_dac_init,
	},
};

/*
 * asoc machine driver
 */
static struct snd_soc_card tau_dac = {
	.name       = "tau_dac",
	.dai_link   = tau_dac_dai,
	.num_links  = ARRAY_SIZE(tau_dac_dai),
};

/*
 * platform device driver
 */
static int tau_dac_parse_dt(struct device_node *np)
{
	int i;

	struct device_node *i2s_node;
	struct device_node *i2c_nodes[ARRAY_SIZE(tau_dac_codecs)];
	struct snd_soc_dai_link *dai = &tau_dac_dai[0];

	i2s_node = of_parse_phandle(np, "i2s-controller", 0);

	if (i2s_node == NULL)
		return -EINVAL;

	dai->cpu_dai_name = NULL;
	dai->cpu_of_node = i2s_node;
	dai->platform_name = NULL;
	dai->platform_of_node = i2s_node;

	for (i = 0; i < ARRAY_SIZE(i2c_nodes); i++) {
		i2c_nodes[i] = of_parse_phandle(np, "codecs", i);

		if (i2c_nodes[i] == NULL)
			return -EINVAL;

		dai->codecs[i].name = NULL;
		dai->codecs[i].of_node = i2c_nodes[i];
	}

    // TODO: get gpio info

//	if (snd_soc_of_parse_card_name(card, "atmel,model") != 0)
//		dev_err(&pdev->dev, "snd_soc_of_parse_card_name() failed\n");

    return 0;
}

static int tau_dac_request_clocks(struct tau_dac_clks *clks, int n,
		struct device *dev)
{
	int i;
	
	// DEBUG
	return 0;

	for (i = 0; i < n; i++) {
		clks[i].ch = devm_clk_get(dev, clks[i].name);
		if (IS_ERR(clks[i].ch))
			return -EINVAL;
	}
	
	return 0;
}

static int tau_dac_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *np = pdev->dev.of_node;

	tau_dac.dev = &pdev->dev;

	/* parse device tree node info */
	if (np != NULL) {
		ret = tau_dac_parse_dt(np);

		if (ret != 0) {
			dev_err(&pdev->dev, "parsing device tree info failed: %d\n", ret);
			goto err_dt;
		}
	} else {
		dev_err(&pdev->dev, "only device tree supported\n");
		return -EINVAL;
	}

	/* request gpio pins */
	ret = gpio_request_array(tau_dac_gpios, ARRAY_SIZE(tau_dac_gpios));

	if (ret != 0) {
		dev_err(&pdev->dev, "gpio_request_array() failed: %d\n", ret);
		goto err_gpio;
	}
	
	/* get clocks */
	ret = tau_dac_request_clocks(tau_dac_lrclks,
			ARRAY_SIZE(tau_dac_lrclks), &pdev->dev);
	
	if (ret != 0) {
		dev_err(&pdev->dev, "getting frame clocks failed: %d\n", ret);
		goto err_clk;
	}
	
	ret = tau_dac_request_clocks(tau_dac_bclks,
			ARRAY_SIZE(tau_dac_bclks), &pdev->dev);

	if (ret != 0) {
		dev_err(&pdev->dev, "getting bit clocks failed: %d\n", ret);
		goto err_clk;
	}
	
	/* register card */
	ret = snd_soc_register_card(&tau_dac);

	if (ret != 0) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n", ret);
		goto err_gpio;
	}

	return ret;
	
err_clk:
err_gpio:
	gpio_free_array(tau_dac_gpios, ARRAY_SIZE(tau_dac_gpios));
err_dt:
	return ret;
}

static int tau_dac_remove(struct platform_device *pdev)
{
	gpio_set_value(TAU_DAC_GPIO_MCLK_ENABLE, 0);
	gpio_set_value(TAU_DAC_GPIO_MCLK_SELECT, 0);
	gpio_free_array(tau_dac_gpios, ARRAY_SIZE(tau_dac_gpios));

	return snd_soc_unregister_card(&tau_dac);
}

static const struct of_device_id tau_dac_of_match[] = {
	{ .compatible = "singularity-audio,tau-dac", },
	{},
};
MODULE_DEVICE_TABLE(of, tau_dac_of_match);

static struct platform_driver tau_dac_driver = {
	.driver = {
		.name  = "snd-tau-dac",
		.owner = THIS_MODULE,
		.of_match_table = tau_dac_of_match,
	},
	.probe  = tau_dac_probe,
	.remove = tau_dac_remove,
};

module_platform_driver(tau_dac_driver);

MODULE_AUTHOR("Sergej Sawazki <taudac@gmx.de>");
MODULE_DESCRIPTION("ASoC Driver for TauDAC");
MODULE_LICENSE("GPL v2");
