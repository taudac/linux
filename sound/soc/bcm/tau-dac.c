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

#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>
#include <linux/clk.h>

#include "../codecs/wm8741.h"

enum {
	BCLK_CPU,
	BCLK_DACL,
	BCLK_DACR,
	LRCLK_CPU,
	LRCLK_DACL,
	LRCLK_DACR,
	NUM_I2S_CLOCKS
};

struct snd_soc_card_drvdata {
	struct clk *mclk24;
	struct clk *mclk22;
	struct clk *mux_mclk;
	bool mclk_enabled;
	struct clk *i2s_clk[NUM_I2S_CLOCKS];
	bool i2s_clk_prepared[NUM_I2S_CLOCKS];
};

/*
 * clocks
 */
static int tau_dac_clk_init(struct snd_soc_card_drvdata *drvdata)
{
	int ret, i;
	struct clk *clkin, *pll, *ms;
	unsigned long clkin_rate, pll_rate, ms_rate;
	const int pll_clkin_ratio = 31;
	const int clkin_ms_ratio = 8;

	for (i = 0; i < NUM_I2S_CLOCKS; i++) {
		ms = clk_get_parent(drvdata->i2s_clk[i]);
		if (IS_ERR(ms))
			return -EINVAL;

		pll = clk_get_parent(ms);
		if (IS_ERR(pll))
			return -EINVAL;

		clkin = clk_get_parent(pll);
		if (IS_ERR(clkin))
			return -EINVAL;

		clkin_rate = clk_get_rate(clkin);
		pll_rate = clkin_rate * pll_clkin_ratio;
		ms_rate = clkin_rate / clkin_ms_ratio;

		ret = clk_set_rate(pll, pll_rate);
		if (ret < 0)
			return ret;

		ret = clk_set_rate(ms, ms_rate);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void tau_dac_clk_unprepare(struct snd_soc_card_drvdata *drvdata)
{
	int i;

	for (i = 0; i < NUM_I2S_CLOCKS; i++) {
		if (drvdata->i2s_clk_prepared[i]) {
			clk_unprepare(drvdata->i2s_clk[i]);
			drvdata->i2s_clk_prepared[i] = false;
		}
	}
}

static int tau_dac_clk_prepare(struct snd_soc_card_drvdata *drvdata)
{
	int ret, i;

	for (i = 0; i < NUM_I2S_CLOCKS; i++) {
		if (!drvdata->i2s_clk_prepared[i]) {
			ret = clk_prepare(drvdata->i2s_clk[i]);
			if (ret != 0)
				return ret;
			drvdata->i2s_clk_prepared[i] = true;
		}
	}

	return 0;
}

static void tau_dac_clk_disable(struct snd_soc_card_drvdata *drvdata)
{
	if (drvdata->mclk_enabled) {
		clk_disable(drvdata->mux_mclk);
		drvdata->mclk_enabled = false;
	}
}

static int tau_dac_clk_enable(struct snd_soc_card_drvdata *drvdata,
		unsigned long mclk_rate, unsigned long bclk_rate,
		unsigned long lrclk_rate)
{
	int ret;

	switch (mclk_rate) {
	case 22579200:
		ret = clk_set_parent(drvdata->mux_mclk, drvdata->mclk22);
		break;
	case 24576000:
		ret = clk_set_parent(drvdata->mux_mclk, drvdata->mclk24);
		break;
	default:
		return -EINVAL;
	}

	if (ret < 0)
		return ret;

	ret = clk_enable(drvdata->mux_mclk);
	if (ret < 0)
		return ret;

	drvdata->mclk_enabled = true;
	msleep(2);

	ret = clk_set_rate(drvdata->i2s_clk[BCLK_CPU], bclk_rate);
	if (ret < 0)
		return ret;

	ret = clk_set_rate(drvdata->i2s_clk[BCLK_DACL], bclk_rate);
	if (ret < 0)
		return ret;

	ret = clk_set_rate(drvdata->i2s_clk[BCLK_DACR], bclk_rate);
	if (ret < 0)
		return ret;

	ret = clk_set_rate(drvdata->i2s_clk[LRCLK_CPU], lrclk_rate);
	if (ret < 0)
		return ret;

	ret = clk_set_rate(drvdata->i2s_clk[LRCLK_DACL], lrclk_rate);
	if (ret < 0)
		return ret;

	ret = clk_set_rate(drvdata->i2s_clk[LRCLK_DACR], lrclk_rate);
	if (ret < 0)
		return ret;

	return 0;
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
	int ret, i;
	int num_codecs = rtd->num_codecs;
	struct snd_soc_dai **codec_dais = rtd->codec_dais;
	struct snd_soc_card_drvdata *drvdata =
			snd_soc_card_get_drvdata(rtd->card);

	unsigned int mclk_freq = 22579200;
	//unsigned int mclk_freq = 24576000;

	ret = tau_dac_clk_init(drvdata);
	if (ret < 0) {
		dev_err(rtd->card->dev, "Initializing clocks failed\n");
		return ret;
	}

	/*  HACK: Due to the codec driver implementation, we have to call
	 *  set_sysclk here. However, we don't yet know which clock to set.
	 *  We need to know the sampling rate to select the master clock.
	 *  TODO: Refactor the codec driver to address this issue.
	 *  eg: move wm8741->sysclk check from startup to hw_params,
	 *  add constraints_all.
	 */
	for (i = 0; i < num_codecs; i++) {
		/* set codecs sysclk */
		ret = snd_soc_dai_set_sysclk(codec_dais[i],
				WM8741_SYSCLK, mclk_freq, SND_SOC_CLOCK_IN);
		if (ret < 0)
			 return ret;
	}

	return 0;
}

static int tau_dac_startup(struct snd_pcm_substream *substream)
{
	return 0;
}

static void tau_dac_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card_drvdata *drvdata =
			snd_soc_card_get_drvdata(rtd->card);

	tau_dac_clk_disable(drvdata);
	tau_dac_clk_unprepare(drvdata);
}

static int tau_dac_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	int ret, i;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card_drvdata *drvdata =
			snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai **codec_dais = rtd->codec_dais;
	int num_codecs = rtd->num_codecs;

	unsigned int mclk_rate, bclk_rate;
	unsigned int lrclk_rate = params_rate(params);
	int width = params_width(params);

	switch (lrclk_rate) {
	case 44100:
	case 88200:
	case 176400:
		mclk_rate = 22579200;
		break;
	case 32000:
	case 48000:
	case 96000:
	case 192000:
		mclk_rate = 24576000;
		break;
	default:
		dev_err(rtd->card->dev, "Rate not supported: %d", lrclk_rate);
		return -EINVAL;
	}

	bclk_rate = 2 * width * lrclk_rate;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_bclk_ratio(cpu_dai, 2 * width);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	for (i = 0; i < num_codecs; i++) {
		/* set codecs sysclk */
		ret = snd_soc_dai_set_sysclk(codec_dais[i],
				WM8741_SYSCLK, mclk_rate, SND_SOC_CLOCK_IN);
		if (ret < 0)
			 return ret;

		/* set codec DAI configuration */
		ret = snd_soc_dai_set_fmt(codec_dais[i], SND_SOC_DAIFMT_I2S |
				SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
		if (ret < 0)
			return ret;
	}

	/* enable clocks*/
	ret = tau_dac_clk_prepare(drvdata);
	if (ret < 0) {
		dev_err(rtd->card->dev, "Preparing clocks failed: %d\n", ret);
		return ret;
	}

	ret = tau_dac_clk_enable(drvdata, mclk_rate, bclk_rate, lrclk_rate);
	if (ret < 0) {
		dev_err(rtd->card->dev, "Starting clocks failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static struct snd_soc_ops tau_dac_ops = {
	.startup   = tau_dac_startup,
	.shutdown  = tau_dac_shutdown,
	.hw_params = tau_dac_hw_params,
};

static struct snd_soc_dai_link tau_dac_dai[] = {
	{
		.name          = "TauDAC I2S",
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
	.name       = "TauDAC",
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
	i2s_node = of_parse_phandle(np, "tau-dac,i2s-controller", 0);

	if (i2s_node == NULL)
		return -EINVAL;

	dai->cpu_dai_name = NULL;
	dai->cpu_of_node = i2s_node;
	dai->platform_name = NULL;
	dai->platform_of_node = i2s_node;

	for (i = 0; i < ARRAY_SIZE(i2c_nodes); i++) {
		i2c_nodes[i] = of_parse_phandle(np, "tau-dac,codecs", i);

		if (i2c_nodes[i] == NULL)
			return -EINVAL;

		dai->codecs[i].name = NULL;
		dai->codecs[i].of_node = i2c_nodes[i];
	}

	return 0;
}

static int tau_dac_set_clk(struct device *dev,
		struct snd_soc_card_drvdata *drvdata)
{
	drvdata->mclk24 = devm_clk_get(dev, "mclk-24M");
	if (IS_ERR(drvdata->mclk24))
		return -EINVAL;

	drvdata->mclk22 = devm_clk_get(dev, "mclk-22M");
	if (IS_ERR(drvdata->mclk22))
		return -EINVAL;

	drvdata->mux_mclk = devm_clk_get(dev, "mux-mclk");
	if (IS_ERR(drvdata->mux_mclk))
		return -EINVAL;
		
	drvdata->i2s_clk[BCLK_CPU] = devm_clk_get(dev, "bclk-cpu");
	if (IS_ERR(drvdata->i2s_clk[BCLK_CPU]))
		return -EPROBE_DEFER;

	drvdata->i2s_clk[BCLK_DACL] = devm_clk_get(dev, "bclk-dacl");
	if (IS_ERR(drvdata->i2s_clk[BCLK_DACL]))
		return -EPROBE_DEFER;

	drvdata->i2s_clk[BCLK_DACR] = devm_clk_get(dev, "bclk-dacr");
	if (IS_ERR(drvdata->i2s_clk[BCLK_DACR]))
		return -EPROBE_DEFER;

	drvdata->i2s_clk[LRCLK_CPU] = devm_clk_get(dev, "lrclk-cpu");
	if (IS_ERR(drvdata->i2s_clk[LRCLK_CPU]))
		return -EPROBE_DEFER;

	drvdata->i2s_clk[LRCLK_DACL] = devm_clk_get(dev, "lrclk-dacl");
	if (IS_ERR(drvdata->i2s_clk[LRCLK_DACL]))
		return -EPROBE_DEFER;

	drvdata->i2s_clk[LRCLK_DACR] = devm_clk_get(dev, "lrclk-dacr");
	if (IS_ERR(drvdata->i2s_clk[LRCLK_DACR]))
		return -EPROBE_DEFER;

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

	/* set clocks */
	ret = tau_dac_set_clk(&pdev->dev, drvdata);
	if (ret != 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Getting clocks failed: %d\n", ret);
		return ret;
	}

	/* register card */
	snd_soc_card_set_drvdata(&tau_dac_card, drvdata);
	snd_soc_of_parse_card_name(&tau_dac_card, "tau-dac,model");
	ret = snd_soc_register_card(&tau_dac_card);
	if (ret != 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "snd_soc_register_card() failed: "
					"%d\n", ret);
		return ret;
	}

	return ret;
}

static int tau_dac_remove(struct platform_device *pdev)
{
	return snd_soc_unregister_card(&tau_dac_card);
}

static const struct of_device_id tau_dac_of_match[] = {
	{ .compatible = "tau-dac,dm101", },
	{},
};
MODULE_DEVICE_TABLE(of, tau_dac_of_match);

static struct platform_driver tau_dac_driver = {
	.driver = {
		.name  = "snd-soc-tau-dac",
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
