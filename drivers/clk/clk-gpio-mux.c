/*
 * Author: Sergej Sawazki <ce3a@gmx.de>
 * Based on clk-gpio-gate.c by Jyri Sarha and ti/mux.c by Tero Kristo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Gpio controlled clock multiplexer implementation
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/device.h>

/**
 * DOC: basic clock multiplexer which can be controlled with a gpio output
 * Traits of this clock:
 * prepare - clk_prepare only ensures that parents are prepared
 * enable - clk_enable and clk_disable are functional & control gpio
 * rate - rate is only affected by parent switching.  No clk_set_rate support
 * parent - parent is adjustable through clk_set_parent
 */

#define to_clk_gpio_mux(_hw) container_of(_hw, struct clk_gpio_mux, hw)

static int clk_gpio_mux_enable(struct clk_hw *hw)
{
	struct clk_gpio_mux *clk = to_clk_gpio_mux(hw);

	if (!clk->gpiod_ena) {
		pr_err("%s: %s: DT property 'enable-gpios' not defined\n",
				__func__, __clk_get_name(hw->clk));
		return -ENOENT;
	}

	gpiod_set_value(clk->gpiod_ena, 1);

	return 0;
}

static void clk_gpio_mux_disable(struct clk_hw *hw)
{
	struct clk_gpio_mux *clk = to_clk_gpio_mux(hw);

	if (!clk->gpiod_ena) {
		pr_err("%s: %s: DT property 'enable-gpios' not defined\n",
				__func__, __clk_get_name(hw->clk));
		return;
	}

	gpiod_set_value(clk->gpiod_ena, 0);
}

static int clk_gpio_mux_is_enabled(struct clk_hw *hw)
{
	struct clk_gpio_mux *clk = to_clk_gpio_mux(hw);

	if (!clk->gpiod_ena) {
		pr_err("%s: %s: DT property 'enable-gpios' not defined\n",
				__func__, __clk_get_name(hw->clk));
		return -EINVAL;
	}

	return gpiod_get_value(clk->gpiod_ena);
}

static u8 clk_gpio_mux_get_parent(struct clk_hw *hw)
{
	struct clk_gpio_mux *clk = to_clk_gpio_mux(hw);

	return gpiod_get_value(clk->gpiod_sel);
}

static int clk_gpio_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_gpio_mux *clk = to_clk_gpio_mux(hw);

	if (index > 1)
		return -EINVAL;

	gpiod_set_value(clk->gpiod_sel, index);

	return 0;
}

const struct clk_ops clk_gpio_mux_ops = {
	.enable = clk_gpio_mux_enable,
	.disable = clk_gpio_mux_disable,
	.is_enabled = clk_gpio_mux_is_enabled,
	.get_parent = clk_gpio_mux_get_parent,
	.set_parent = clk_gpio_mux_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};
EXPORT_SYMBOL_GPL(clk_gpio_mux_ops);

/**
 * clk_register_gpio_mux - register a gpio clock mux with the clock framework
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @parent_names: names of this clock's parents
 * @num_parents: number of parents listed in @parent_names
 * @gpiod_sel: gpio descriptor to select the parent of this clock multiplexer
 * @gpiod_ena: gpio descriptor to enable the output of this clock multiplexer
 * @clk_flags: optional flags for basic clock
 */
struct clk *clk_register_gpio_mux(struct device *dev, const char *name,
		const char **parent_names, u8 num_parents,
		struct gpio_desc *gpiod_sel, struct gpio_desc *gpiod_ena,
		unsigned long clk_flags)
{
	struct clk_gpio_mux *clk_gpio_mux = NULL;
	struct clk *clk = ERR_PTR(-EINVAL);
	struct clk_init_data init = { NULL };
	unsigned long gpio_sel_flags, gpio_ena_flags;
	int err;

	if (dev)
		clk_gpio_mux = devm_kzalloc(dev, sizeof(struct clk_gpio_mux),
					GFP_KERNEL);
	else
		clk_gpio_mux = kzalloc(sizeof(struct clk_gpio_mux), GFP_KERNEL);

	if (!clk_gpio_mux)
		return ERR_PTR(-ENOMEM);

	if (gpiod_is_active_low(gpiod_sel))
		gpio_sel_flags = GPIOF_OUT_INIT_HIGH;
	else
		gpio_sel_flags = GPIOF_OUT_INIT_LOW;

	if (dev)
		err = devm_gpio_request_one(dev, desc_to_gpio(gpiod_sel),
				gpio_sel_flags, name);
	else
		err = gpio_request_one(desc_to_gpio(gpiod_sel),
				gpio_sel_flags, name);

	if (err) {
		pr_err("%s: %s: Error requesting gpio %u\n",
		       __func__, name, desc_to_gpio(gpiod_sel));
		return ERR_PTR(err);
	}
	clk_gpio_mux->gpiod_sel = gpiod_sel;

	if (gpiod_ena != NULL) {
		if (gpiod_is_active_low(gpiod_ena))
			gpio_ena_flags = GPIOF_OUT_INIT_HIGH;
		else
			gpio_ena_flags = GPIOF_OUT_INIT_LOW;

		if (dev)
			err = devm_gpio_request_one(dev,
					desc_to_gpio(gpiod_ena),
					gpio_ena_flags, name);
		else
			err = gpio_request_one(desc_to_gpio(gpiod_ena),
					gpio_ena_flags, name);

		if (err) {
			pr_err("%s: %s: Error requesting gpio %u\n",
				   __func__, name, desc_to_gpio(gpiod_ena));
			return ERR_PTR(err);
		}
		clk_gpio_mux->gpiod_ena = gpiod_ena;
	}

	init.name = name;
	init.ops = &clk_gpio_mux_ops;
	init.flags = clk_flags | CLK_IS_BASIC;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	clk_gpio_mux->hw.init = &init;

	clk = clk_register(dev, &clk_gpio_mux->hw);

	if (!IS_ERR(clk))
		return clk;

	if (!dev) {
		kfree(clk_gpio_mux);
		gpiod_put(gpiod_ena);
		gpiod_put(gpiod_sel);
	}

	return clk;
}
EXPORT_SYMBOL_GPL(clk_register_gpio_mux);

#ifdef CONFIG_OF
/**
 * The clk_register_gpio_mux has to be delayed, because the EPROBE_DEFER
 * can not be handled properly at of_clk_init() call time.
 */

struct clk_gpio_mux_delayed_register_data {
	struct device_node *node;
	struct mutex lock;
	struct clk *clk;
};

static struct clk *of_clk_gpio_mux_delayed_register_get(
		struct of_phandle_args *clkspec,
		void *_data)
{
	struct clk_gpio_mux_delayed_register_data *data = _data;
	struct clk *clk = ERR_PTR(-EINVAL);
	const char *clk_name = data->node->name;
	int i, num_parents;
	const char **parent_names;
	struct gpio_desc *gpiod_sel, *gpiod_ena;
	int gpio;
	u32 flags = 0;

	mutex_lock(&data->lock);

	if (data->clk) {
		mutex_unlock(&data->lock);
		return data->clk;
	}

	gpio = of_get_named_gpio_flags(data->node, "select-gpios", 0, NULL);
	if (!gpio_is_valid(gpio))
		goto err_gpio;
	gpiod_sel = gpio_to_desc(gpio);

	gpio = of_get_named_gpio_flags(data->node, "enable-gpios", 0, NULL);
	if (!gpio_is_valid(gpio)) {
		if (gpio != -ENOENT)
			goto err_gpio;
		else
			gpiod_ena = NULL;
	} else {
		gpiod_ena = gpio_to_desc(gpio);
	}

	num_parents = of_clk_get_parent_count(data->node);
	if (num_parents < 2) {
		pr_err("mux-clock %s must have 2 parents\n", data->node->name);
		return clk;
	}

	parent_names = kzalloc((sizeof(char *) * num_parents), GFP_KERNEL);
	if (!parent_names) {
		kfree(parent_names);
		return clk;
	}

	for (i = 0; i < num_parents; i++)
		parent_names[i] = of_clk_get_parent_name(data->node, i);
	
	clk = clk_register_gpio_mux(NULL, clk_name, parent_names, num_parents,
			gpiod_sel, gpiod_ena, flags);
	if (IS_ERR(clk)) {
		mutex_unlock(&data->lock);
		return clk;
	}

	data->clk = clk;
	mutex_unlock(&data->lock);

	return clk;

err_gpio:
	mutex_unlock(&data->lock);
	if (gpio == -EPROBE_DEFER)
		pr_warning("%s: %s: GPIOs not yet available, retry later\n",
				__func__, clk_name);
	else
		pr_err("%s: %s: Can't get GPIOs\n",
				__func__, clk_name);
	return ERR_PTR(gpio);
}

/**
 * of_gpio_mux_clk_setup() - Setup function for gpio controlled clock mux
 */
void __init of_gpio_mux_clk_setup(struct device_node *node)
{
	struct clk_gpio_mux_delayed_register_data *data;

	data = kzalloc(sizeof(struct clk_gpio_mux_delayed_register_data),
		       GFP_KERNEL);
	if (!data)
		return;

	data->node = node;
	mutex_init(&data->lock);

	of_clk_add_provider(node, of_clk_gpio_mux_delayed_register_get, data);
}
EXPORT_SYMBOL_GPL(of_gpio_mux_clk_setup);
CLK_OF_DECLARE(gpio_mux_clk, "gpio-mux-clock", of_gpio_mux_clk_setup);
#endif
