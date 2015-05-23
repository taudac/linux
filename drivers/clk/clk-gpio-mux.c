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
 * rate - rate is only affected by parent switching.  No clk_set_rate support
 * parent - parent is adjustable through clk_set_parent
 */

#define to_clk_gpio_mux(_hw) container_of(_hw, struct clk_gpio_mux, hw)

static u8 clk_gpio_mux_get_parent(struct clk_hw *hw)
{
	struct clk_gpio_mux *clk = to_clk_gpio_mux(hw);

	return gpiod_get_value(clk->gpiod);
}

static int clk_gpio_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_gpio_mux *clk = to_clk_gpio_mux(hw);

	gpiod_set_value(clk->gpiod, index);
	pr_debug("%s: %s: index = %d\n",
			__clk_get_name(hw->clk), __func__, index);

	return 0;
}

const struct clk_ops clk_gpio_mux_ops = {
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
 * @gpiod: gpio descriptor to select the parent of this clock multiplexer
 * @clk_flags: optional flags for basic clock
 */
struct clk *clk_register_gpio_mux(struct device *dev, const char *name,
		const char **parent_names, u8 num_parents,
		struct gpio_desc *gpiod, unsigned long clk_flags)
{
	struct clk_gpio_mux *clk_gpio_mux = NULL;
	struct clk *clk = ERR_PTR(-EINVAL);
	struct clk_init_data init = { NULL };
	unsigned long gpio_flags;
	int err;

	if (dev)
		clk_gpio_mux = devm_kzalloc(dev, sizeof(struct clk_gpio_mux),
				GFP_KERNEL);
	else
		clk_gpio_mux = kzalloc(sizeof(struct clk_gpio_mux), GFP_KERNEL);

	if (!clk_gpio_mux)
		return ERR_PTR(-ENOMEM);

	if (gpiod_is_active_low(gpiod))
		gpio_flags = GPIOF_OUT_INIT_HIGH;
	else
		gpio_flags = GPIOF_OUT_INIT_LOW;

	if (dev)
		err = devm_gpio_request_one(dev, desc_to_gpio(gpiod),
				gpio_flags, name);
	else
		err = gpio_request_one(desc_to_gpio(gpiod),
				gpio_flags, name);

	if (err) {
		if (err != -EPROBE_DEFER)
			pr_err("%s: %s: Error requesting gpio %u\n",
					name, __func__, desc_to_gpio(gpiod));
		return ERR_PTR(err);
	}
	clk_gpio_mux->gpiod = gpiod;

	init.name = name;
	init.ops = &clk_gpio_mux_ops;
	init.flags = clk_flags | CLK_IS_BASIC;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	clk_gpio_mux->hw.init = &init;

	if (dev)
		clk = devm_clk_register(dev, &clk_gpio_mux->hw);
	else
		clk = clk_register(NULL, &clk_gpio_mux->hw);

	if (!IS_ERR(clk)) {
		pr_debug("%s: %s: Successful registration\n", name, __func__);
		return clk;
	}

	if (!dev) {
		kfree(clk_gpio_mux);
		gpiod_put(gpiod);
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
	struct gpio_desc *gpiod;
	int gpio;

	mutex_lock(&data->lock);

	if (data->clk) {
		mutex_unlock(&data->lock);
		return data->clk;
	}

	gpio = of_get_named_gpio_flags(data->node, "select-gpios", 0, NULL);
	if (!gpio_is_valid(gpio))
		goto err_gpio;
	gpiod = gpio_to_desc(gpio);

	num_parents = of_clk_get_parent_count(data->node);
	if (num_parents != 2) {
		pr_err("mux-clock %s must have 2 parents\n", data->node->name);
		return clk;
	}

	parent_names = kcalloc(num_parents, sizeof(char *), GFP_KERNEL);
	if (!parent_names) {
		kfree(parent_names);
		return clk;
	}

	for (i = 0; i < num_parents; i++)
		parent_names[i] = of_clk_get_parent_name(data->node, i);

	clk = clk_register_gpio_mux(NULL, clk_name, parent_names, num_parents,
			gpiod, 0);
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
		pr_debug("%s: %s: GPIOs not yet available, retry later\n",
				clk_name, __func__);
	else
		pr_err("%s: %s: Can't get GPIOs\n",
				clk_name, __func__);

	return ERR_PTR(gpio);
}

/**
 * of_gpio_mux_clk_setup() - Setup function for gpio controlled clock mux
 */
void __init of_gpio_mux_clk_setup(struct device_node *node)
{
	struct clk_gpio_mux_delayed_register_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return;

	data->node = node;
	mutex_init(&data->lock);

	of_clk_add_provider(node, of_clk_gpio_mux_delayed_register_get, data);
}
EXPORT_SYMBOL_GPL(of_gpio_mux_clk_setup);
CLK_OF_DECLARE(gpio_mux_clk, "gpio-mux-clock", of_gpio_mux_clk_setup);
#endif
