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

enum {
	MCLK24,
	MCLK22,
	BCLK_CPU,
	BCLK_DACL,
	BCLK_DACR,
	LRCLK_CPU,
	LRCLK_DACL,
	LRCLK_DACR,
	MAX_I2S_CLOCKS
};

struct snd_soc_card_drvdata {
	unsigned gpio_mclk_sel;
	unsigned gpio_mclk_ena;
	struct clk *i2s_clk[MAX_I2S_CLOCKS];
};

static int tau_dac_enable_mclk(struct snd_soc_card_drvdata *drvdata,
		unsigned int freq)
{
	int index;

	switch (freq) {
	case 22579200:
		index = 0;
		break;
	case 24576000:
		index = 1;
		break;
	default:
		return -EINVAL;
	}
	
	/* WTF???
	ret = clk_set_rate(drvdata->i2s_clk[MCLK], freq);
	if (ret != 0)
		printk(KERN_DBG "clk_set_rate failed %d", ret);
	
	
	//clk_set_parent(drvdata->i2s_clk[BCLK_CPU], drvdata->i2s_clk[MCLK]);
	*/
	
	/*----------------
	struct si5351_platform_data *pdata;
	clk_set_rate(pdata->clk_clkin, 123456);
	*/

	gpio_set_value(drvdata->gpio_mclk_sel, index);
	gpio_set_value(drvdata->gpio_mclk_ena, 1);
	// TODO: msleep(20);
	
	return 0;
}

static void tau_dac_disable_mclk(struct snd_soc_card_drvdata *drvdata)
{
	gpio_set_value(drvdata->gpio_mclk_ena, 0);
}

static int tau_dac_prepare_i2s_clk(struct snd_soc_card_drvdata *drvdata)
{
	int ret, i;
	
	// DEBUG
	return 0;

	for (i = 0; i < MAX_I2S_CLOCKS; i++) {
		ret = clk_prepare(drvdata->i2s_clk[i]);
		if (ret != 0)
			return ret;
	}

	return 0;
}

static void tau_dac_unprepare_i2s_clk(struct snd_soc_card_drvdata *drvdata)
{
	int i;

	for (i = 0; i < MAX_I2S_CLOCKS; i++)
		clk_unprepare(drvdata->i2s_clk[i]);
}

static int tau_dac_enable_i2s_clk(struct snd_soc_card_drvdata *drvdata,
		unsigned long lrclk_rate, unsigned long bclk_rate)
{
	int ret, i;

	ret = clk_set_rate(drvdata->i2s_clk[LRCLK_CPU], lrclk_rate);
	if (ret != 0)
		return ret;

	ret = clk_set_rate(drvdata->i2s_clk[LRCLK_DACL], lrclk_rate);
	if (ret != 0)
		return ret;

	ret = clk_set_rate(drvdata->i2s_clk[LRCLK_DACR], lrclk_rate);
	if (ret != 0)
		return ret;

	ret = clk_set_rate(drvdata->i2s_clk[BCLK_CPU], bclk_rate);
	if (ret != 0)
		return ret;

	ret = clk_set_rate(drvdata->i2s_clk[BCLK_DACL], bclk_rate);
	if (ret != 0)
		return ret;

	ret = clk_set_rate(drvdata->i2s_clk[BCLK_DACR], bclk_rate);
	if (ret != 0)
		return ret;

	/* NOT used in si5351 ???
	for (i = 0; i < MAX_I2S_CLOCKS; i++) {
		ret = clk_enable(drvdata->i2s_clk[i]);
		if (ret != 0)
			return ret;
	}
	*/

	return 0;
}

static void tau_dac_disable_i2s_clk(struct snd_soc_card_drvdata *drvdata)
{
	int i;

	for (i = 0; i < MAX_I2S_CLOCKS; i++)
		clk_disable(drvdata->i2s_clk[i]);
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
	int ret = 0, i;
	int num_codecs = rtd->num_codecs;
	struct snd_soc_dai **codec_dais = rtd->codec_dais;
	
	// DEBUG
	struct snd_soc_card_drvdata *drvdata = 
			snd_soc_card_get_drvdata(rtd->card);

	unsigned int mclk_freq = 22579200;
	//unsigned int mclk_freq = 24576000;

	/*  HACK: Due to the codec driver implementation, we have to call
	 *  set_sysclk here. However, we don't yet know which clock to set.
	 *  We need to know the sampling rate to select the master clock.
	 *  TODO: refactor the codec driver to address this issue
	 */
	for (i = 0; i < num_codecs; i++) {
		/* set codecs sysclk */
		ret = snd_soc_dai_set_sysclk(codec_dais[i],
				WM8741_SYSCLK, mclk_freq, SND_SOC_CLOCK_IN);
		if (ret != 0)
			 return ret;
	}
	
	struct clk *clkin = clk_get_parent(drvdata->i2s_clk[BCLK_CPU]);
	struct clk *clkinp = clk_get_parent(clkin);
	
	// DEBUG
	printk(KERN_DEBUG "osc22  = %p\n", drvdata->i2s_clk[MCLK22]);
	printk(KERN_DEBUG "osc24  = %p\n", drvdata->i2s_clk[MCLK24]);
//	printk(KERN_DEBUG "bclk   = %p\n", drvdata->i2s_clk[BCLK_CPU]);
//	printk(KERN_DEBUG "clkin  = %p\n", clkin);
	printk(KERN_DEBUG "clkinp = %p\n", clkinp);
	
	
	//tau_dac_disable_i2s_clk(drvdata);
	//tau_dac_unprepare_i2s_clk(drvdata);
	
	
	
	ret = clk_set_parent(clkin, drvdata->i2s_clk[MCLK24]);
	if (ret != 0)
		printk(KERN_DEBUG "clk_set_parent failed: %d\n", ret);
	clkinp = clk_get_parent(clkin);
	printk(KERN_DEBUG "~~~~~~\n");
	printk(KERN_DEBUG "clkinp = %p\n", clkinp);
	
	ret = clk_set_parent(clkin, drvdata->i2s_clk[MCLK22]);
	if (ret != 0)
		printk(KERN_DEBUG "clk_set_parent failed again: %d\n", ret);
	else
		printk(KERN_DEBUG "clk_set_parent succeded: %d\n", ret);


	return ret;
}

static int tau_dac_startup(struct snd_pcm_substream *substream)
{
	return 0;
}

static void tau_dac_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *soc_card = rtd->card;
	struct snd_soc_card_drvdata *drvdata = snd_soc_card_get_drvdata(soc_card);

	tau_dac_disable_i2s_clk(drvdata);
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

	unsigned int mclk_freq, bclk_freq, lrclk_freq;
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
		dev_err(soc_card->dev, "Rate not supported: %d", rate);
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

	/* enable clocks*/
	bclk_freq = rate * width;
	lrclk_freq = rate;

	tau_dac_enable_mclk(drvdata, mclk_freq);

	ret = tau_dac_enable_i2s_clk(drvdata, bclk_freq, lrclk_freq);
	if (ret != 0) {
		dev_err(soc_card->dev, "Starting I2S clocks failed: %d\n", ret);
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
		const char *name, unsigned *gpio, int *flags)
{
	enum of_gpio_flags of_flags;
	int of_gpio;

	if (of_find_property(np, name, NULL)) {
		of_gpio = of_get_named_gpio_flags(np, name, 0, &of_flags);
		if (!gpio_is_valid(of_gpio))
			return -EINVAL;

		*gpio = of_gpio;
		*flags = (of_flags & OF_GPIO_ACTIVE_LOW) ? GPIOF_OUT_INIT_HIGH :
				GPIOF_OUT_INIT_LOW;

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
	drvdata->i2s_clk[MCLK24] = devm_clk_get(dev, "mclk24");
	if (IS_ERR(drvdata->i2s_clk[MCLK24]))
		return -EINVAL;

	drvdata->i2s_clk[MCLK22] = devm_clk_get(dev, "mclk22");
	if (IS_ERR(drvdata->i2s_clk[MCLK22]))
		return -EINVAL;
		
	drvdata->i2s_clk[LRCLK_CPU] = devm_clk_get(dev, "lrclk-cpu");
	if (IS_ERR(drvdata->i2s_clk[LRCLK_CPU]))
		return -EINVAL;

	drvdata->i2s_clk[LRCLK_DACL] = devm_clk_get(dev, "lrclk-dacl");
	if (IS_ERR(drvdata->i2s_clk[LRCLK_CPU]))
		return -EINVAL;

	drvdata->i2s_clk[LRCLK_DACR] = devm_clk_get(dev, "lrclk-dacr");
	if (IS_ERR(drvdata->i2s_clk[LRCLK_CPU]))
		return -EINVAL;

	drvdata->i2s_clk[BCLK_CPU] = devm_clk_get(dev, "bclk-cpu");
	if (IS_ERR(drvdata->i2s_clk[LRCLK_CPU]))
		return -EINVAL;

	drvdata->i2s_clk[BCLK_DACL] = devm_clk_get(dev, "bclk-dacl");
	if (IS_ERR(drvdata->i2s_clk[LRCLK_CPU]))
		return -EINVAL;

	drvdata->i2s_clk[BCLK_DACR] = devm_clk_get(dev, "bclk-dacr");
	if (IS_ERR(drvdata->i2s_clk[LRCLK_CPU]))
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

	/* prepare clocks */
	ret = tau_dac_prepare_i2s_clk(drvdata);
	if (ret != 0) {
		dev_err(&pdev->dev, "Preparing clocks failed: %d\n", ret);
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
	struct snd_soc_card_drvdata *drvdata = snd_soc_card_get_drvdata(&tau_dac_card);

	/* unprepare clocks */
	tau_dac_unprepare_i2s_clk(drvdata);

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
