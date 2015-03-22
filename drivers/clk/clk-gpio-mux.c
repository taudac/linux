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
 * DOC: basic clock multiplexer which can be controlled
 *      with gpio output
 * Traits of this clock:
 * prepare - clk_prepare only ensures that parents are prepared
 * enable - clk_enable only ensures that parents are enabled
 * rate - rate is only affected by parent switching.  No clk_set_rate support
 * parent - parent is adjustable through clk_set_parent
 */

#define to_clk_mux(_hw) container_of(_hw, struct clk_mux, hw)

static u8 clk_mux_get_parent(struct clk_hw *hw)
{
	printk(KERN_DBG "%s", __FUNCTION__);
	return 0;
}

static int clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	printk(KERN_DBG "%s", __FUNCTION__)
	return 0;
}

const struct clk_ops clk_mux_ops = {
	.get_parent = clk_mux_get_parent,
	.set_parent = clk_mux_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};
EXPORT_SYMBOL_GPL(clk_mux_ops);

const struct clk_ops clk_mux_ro_ops = {
	.get_parent = clk_mux_get_parent,
};
EXPORT_SYMBOL_GPL(clk_mux_ro_ops);
;

/**
 * clk_register_gpio_mux - register a gpio mux clock with the clock framework
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @parent_names: names of this clock's parents
 * @num_parents: number of parents listed in @parent_names
 * @gpiod: gpio descriptor to select the parent of this clock
 * @flags: optional flags for basic clock
 */
struct clk *clk_register_gpio_mux(struct device *dev, const char *name,
		const char **parent_names, u8 num_parents, struct gpio_desc *gpiod,
		unsigned long flags)
{
	struct clk_gpio *clk_gpio = NULL;
	struct clk *clk = ERR_PTR(-EINVAL);
	struct clk_init_data init = { NULL };
	unsigned long gpio_flags;
	int err;

	if (gpiod_is_active_low(gpiod))
		gpio_flags = GPIOF_OUT_INIT_HIGH;
	else
		gpio_flags = GPIOF_OUT_INIT_LOW;

	if (dev)
		err = devm_gpio_request_one(dev, desc_to_gpio(gpiod),
				gpio_flags, name);
	else
		err = gpio_request_one(desc_to_gpio(gpiod), gpio_flags, name);

	if (err) {
		pr_err("%s: %s: Error requesting clock control gpio %u\n",
		       __func__, name, desc_to_gpio(gpiod));
		return ERR_PTR(err);
	}

	if (dev)
		clk_gpio = devm_kzalloc(dev, sizeof(struct clk_gpio),
					GFP_KERNEL);
	else
		clk_gpio = kzalloc(sizeof(struct clk_gpio), GFP_KERNEL);

	if (!clk_gpio) {
		clk = ERR_PTR(-ENOMEM);
		goto clk_register_gpio_gate_err;
	}

	init.name = name;
	init.ops = &clk_gpio_gate_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	clk_gpio->gpiod = gpiod;
	clk_gpio->hw.init = &init;

	clk = clk_register(dev, &clk_gpio->hw);

	if (!IS_ERR(clk))
		return clk;

	if (!dev)
		kfree(clk_gpio);

clk_register_gpio_gate_err:
	gpiod_put(gpiod);

	return clk;
}
EXPORT_SYMBOL_GPL(clk_register_gpio_mux);

#ifdef CONFIG_OF
/**
 * The clk_register_gpio_mux has to be delayed, because the EPROBE_DEFER
 * can not be handled properly at of_clk_init() call time.
 */

struct clk_gpio_gate_delayed_register_data {
	struct device_node *node;
	struct mutex lock;
	struct clk *clk;
};

static struct clk *of_clk_gpio_mux_delayed_register_get(
		struct of_phandle_args *clkspec,
		void *_data)
{
	struct clk_gpio_gate_delayed_register_data *data = _data;
	struct clk *clk;
	const char *clk_name = data->node->name;
	const char *parent_name;
	struct gpio_desc *gpiod;
	int gpio;

	mutex_lock(&data->lock);

	if (data->clk) {
		mutex_unlock(&data->lock);
		return data->clk;
	}

	gpio = of_get_named_gpio_flags(data->node, "select-gpios", 0, NULL);
	if (gpio < 0) {
		mutex_unlock(&data->lock);
		if (gpio != -EPROBE_DEFER)
			pr_err("%s: %s: Can't get 'select-gpios' DT property\n",
			       __func__, clk_name);
		return ERR_PTR(gpio);
	}
	gpiod = gpio_to_desc(gpio);

	/*
	parent_name = of_clk_get_parent_name(data->node, 0);
	*/
	
	num_parents = of_clk_get_parent_count(node);
	if (num_parents < 2) {
		pr_err("mux-clock %s must have parents\n", node->name);
		return clk;
	}
	parent_names = kzalloc((sizeof(char *) * num_parents), GFP_KERNEL);
	if (!parent_names)
		goto cleanup;

	for (i = 0; i < num_parents; i++)
		parent_names[i] = of_clk_get_parent_name(node, i);
	
	clk = clk_register_mux_gate(NULL, clk_name, parent_name, gpiod, 0);
	if (IS_ERR(clk)) {
		mutex_unlock(&data->lock);
		return clk;
	}

	data->clk = clk;
	mutex_unlock(&data->lock);

	return clk;
	
cleanup:
	kfree(mux);
}

/**
 * of_gpio_mux_clk_setup() - Setup function for gpio controlled clock mux
 */
void __init of_gpio_mux_clk_setup(struct device_node *node)
{
	struct clk_gpio_gate_delayed_register_data *data;

	data = kzalloc(sizeof(struct clk_gpio_gate_delayed_register_data),
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
