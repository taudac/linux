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

#include <linux/of_gpio.h>
#include <linux/i2c.h>
#include <linux/clk.h>

#include "../codecs/wm8741.h"

struct snd_soc_card_drvdata {
	unsigned gpio_mclk_sel;
	unsigned gpio_mclk_ena;
	struct clk* lrclk_cpu;
	struct clk* lrclk_dacl;
	struct clk* lrclk_dacr;
	struct clk* bclk_cpu;
	struct clk* bclk_dacl;
	struct clk* bclk_dacr;
};

static void tau_dac_enable_mclk(struct snd_soc_card_drvdata* drvdata,
		unsigned int freq)
{
	int index;

	switch (freq) {
	case 22579200: index = 0; break;
	case 24576000: index = 1; break;
	default: return;
	}

	gpio_set_value(drvdata->gpio_mclk_sel, index);
	gpio_set_value(drvdata->gpio_mclk_ena, 1);
	// TODO: msleep(20);
}

static void tau_dac_disable_mclk(struct snd_soc_card_drvdata* drvdata)
{
	gpio_set_value(drvdata->gpio_mclk_ena, 0);
}

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
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *soc_card = rtd->card;
	struct snd_soc_card_drvdata *drvdata = snd_soc_card_get_drvdata(soc_card);

	tau_dac_disable_mclk(drvdata);
}

static int tau_dac_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	int ret, i;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *soc_card = rtd->card;
	struct snd_soc_card_drvdata *drvdata = snd_soc_card_get_drvdata(soc_card);
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
	
	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_bclk_ratio(cpu_dai, 32 * 2);
	if (ret != 0)
		return ret;
	
	ret = snd_soc_dai_set_fmt(cpu_dai,
		SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret != 0)
		return ret;
	
	for (i = 0; i < num_codecs; i++) {
		/* set codecs sysclk */
		ret = snd_soc_dai_set_sysclk(codec_dais[i],
			WM8741_SYSCLK, mclk_freq, SND_SOC_CLOCK_IN);
		if (ret != 0)
			 return ret;

		/* set codec DAI configuration */
		ret = snd_soc_dai_set_fmt(codec_dais[i],
			SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
		if (ret != 0)
			return ret;
	}

	// TODO: enable mclk, lrclk, bclk
	tau_dac_enable_mclk(drvdata, mclk_freq);
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
static struct snd_soc_card tau_dac_card = {
	.name       = "tau_dac",
	.dai_link   = tau_dac_dai,
	.num_links  = ARRAY_SIZE(tau_dac_dai),
};

/*
 * platform device driver
 */
static int tau_dac_set_dai(struct device_node *np)
{
	int i;

	struct device_node *i2s_node;
	struct device_node *i2c_nodes[ARRAY_SIZE(tau_dac_codecs)];
	struct snd_soc_dai_link *dai = &tau_dac_dai[0];

	/* dais */
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
		
//	if (snd_soc_of_parse_card_name(card, "atmel,model") != 0)
//		dev_err(&pdev->dev, "snd_soc_of_parse_card_name() failed\n");

    return 0;
}

static int tau_dac_get_gpio_from_of(struct device_node *np,
		const char* name, unsigned* gpio, int* flags)
{
	enum of_gpio_flags of_flags;
	int of_gpio;

	if (of_find_property(np, name, NULL)) {
		of_gpio = of_get_named_gpio_flags(np, name, 0, &of_flags);
		if (!gpio_is_valid(of_gpio))
			return -EINVAL;

		*gpio = of_gpio;
		*flags = (of_flags & OF_GPIO_ACTIVE_LOW) ? GPIOF_OUT_INIT_LOW :
				GPIOF_OUT_INIT_HIGH;

		return 0;
	}

	return -EINVAL;
}

static int tau_dac_set_gpios(struct device *dev,
		struct snd_soc_card_drvdata *drvdata)
{
	int ret = 0;
	int flags;

	ret |= tau_dac_get_gpio_from_of(dev->of_node, "gpio-mclk-sel",
			&drvdata->gpio_mclk_sel, &flags);

	ret |= devm_gpio_request_one(dev, drvdata->gpio_mclk_sel, flags,
			"tau-dac_mclk-sel");

	ret |= tau_dac_get_gpio_from_of(dev->of_node, "gpio-mclk-ena",
				&drvdata->gpio_mclk_ena, &flags);

	ret |= devm_gpio_request_one(dev, drvdata->gpio_mclk_ena, flags,
			"tau-dac_mclk-ena");

	return ret;
}

static int tau_dac_set_clocks(struct device *dev,
		struct snd_soc_card_drvdata *drvdata)
{
	drvdata->lrclk_cpu = devm_clk_get(dev, "lrclk-cpu");
	if (IS_ERR(drvdata->lrclk_cpu))
		return -EINVAL;

	drvdata->lrclk_dacl = devm_clk_get(dev, "lrclk-dacl");
	if (IS_ERR(drvdata->lrclk_dacl))
		return -EINVAL;

	drvdata->lrclk_dacr = devm_clk_get(dev, "lrclk-dacr");
	if (IS_ERR(drvdata->lrclk_dacr))
		return -EINVAL;

	drvdata->bclk_cpu = devm_clk_get(dev, "bclk-cpu");
	if (IS_ERR(drvdata->bclk_cpu))
		return -EINVAL;

	drvdata->bclk_dacl = devm_clk_get(dev, "bclk-dacl");
	if (IS_ERR(drvdata->bclk_dacl))
		return -EINVAL;

	drvdata->bclk_dacr = devm_clk_get(dev, "bclk-dacr");
	if (IS_ERR(drvdata->bclk_dacr))
		return -EINVAL;

	return 0;
}

static int tau_dac_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *np;
	struct snd_soc_card_drvdata *drvdata;

	tau_dac_card.dev = &pdev->dev;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (drvdata == NULL)
		return -ENOMEM;

	np = pdev->dev.of_node;
	if (np == NULL) {
		dev_err(&pdev->dev, "Device tree node not found\n");
		return -ENODEV;
	}

	/* set dai */
	ret = tau_dac_set_dai(np);
	if (ret != 0) {
		dev_err(&pdev->dev, "Setting dai failed: %d\n", ret);
		return ret;
	}

	/* set gpios */
	ret = tau_dac_set_gpios(&pdev->dev, drvdata);
	if (ret != 0) {
		dev_err(&pdev->dev, "Setting gpios failed: %d\n", ret);
		return ret;
	}

	/* set clocks */
	ret = tau_dac_set_clocks(&pdev->dev, drvdata);
	if (ret != 0) {
		dev_err(&pdev->dev, "Setting clocks failed: %d\n", ret);
		return ret;
	}
	
	/* register card */
	snd_soc_card_set_drvdata(&tau_dac_card, drvdata);
	ret = snd_soc_register_card(&tau_dac_card);

	if (ret != 0) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n", ret);
		return ret;
	}

	return ret;
}

static int tau_dac_remove(struct platform_device *pdev)
{
	return snd_soc_unregister_card(&tau_dac_card);
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
