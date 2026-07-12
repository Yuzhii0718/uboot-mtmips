// SPDX-License-Identifier: GPL-2.0+
/*
 * Atheros ATH79 GPIO Driver
 *
 * Copyright (C) 2026 Yuzhii0178 <admin@yuzhii0178.eu.org>
 *
 * Supports QCA953X, QCA955X, QCA956X and similar SoCs.
 *
 * Register layout (all offsets relative to GPIO base 0x18040000):
 *   OE    0x00  — Output Enable  (bit=1 → input, bit=0 → output)
 *   IN    0x04  — Input value
 *   OUT   0x08  — Output value
 *   SET   0x0c  — Output set   (write 1 to set)
 *   CLEAR 0x10  — Output clear (write 1 to clear)
 */

#include <dm.h>
#include <errno.h>
#include <asm/io.h>
#include <asm/gpio.h>
#include <dt-bindings/gpio/gpio.h>

#define ATH79_GPIO_REG_OE		0x00
#define ATH79_GPIO_REG_IN		0x04
#define ATH79_GPIO_REG_OUT		0x08
#define ATH79_GPIO_REG_SET		0x0c
#define ATH79_GPIO_REG_CLEAR		0x10

struct ath79_gpio_priv {
	void __iomem *regs;
	int gpio_count;
};

static int ath79_gpio_get_value(struct udevice *dev, unsigned int offset)
{
	struct ath79_gpio_priv *priv = dev_get_priv(dev);

	return !!(readl(priv->regs + ATH79_GPIO_REG_IN) & BIT(offset));
}

static int ath79_gpio_set_value(struct udevice *dev, unsigned int offset,
				int value)
{
	struct ath79_gpio_priv *priv = dev_get_priv(dev);

	writel(BIT(offset), priv->regs +
	       (value ? ATH79_GPIO_REG_SET : ATH79_GPIO_REG_CLEAR));

	return 0;
}

static int ath79_gpio_direction_input(struct udevice *dev, unsigned int offset)
{
	struct ath79_gpio_priv *priv = dev_get_priv(dev);

	/* OE bit=1 → input */
	setbits_le32(priv->regs + ATH79_GPIO_REG_OE, BIT(offset));

	return 0;
}

static int ath79_gpio_direction_output(struct udevice *dev, unsigned int offset,
				       int value)
{
	struct ath79_gpio_priv *priv = dev_get_priv(dev);

	/* Set output value before switching direction */
	ath79_gpio_set_value(dev, offset, value);

	/* OE bit=0 → output */
	clrbits_le32(priv->regs + ATH79_GPIO_REG_OE, BIT(offset));

	return 0;
}

static int ath79_gpio_get_function(struct udevice *dev, unsigned int offset)
{
	struct ath79_gpio_priv *priv = dev_get_priv(dev);
	u32 oe = readl(priv->regs + ATH79_GPIO_REG_OE);

	return (oe & BIT(offset)) ? GPIOF_INPUT : GPIOF_OUTPUT;
}

static const struct dm_gpio_ops ath79_gpio_ops = {
	.direction_input	= ath79_gpio_direction_input,
	.direction_output	= ath79_gpio_direction_output,
	.get_value		= ath79_gpio_get_value,
	.set_value		= ath79_gpio_set_value,
	.get_function		= ath79_gpio_get_function,
};

static int ath79_gpio_probe(struct udevice *dev)
{
	struct ath79_gpio_priv *priv = dev_get_priv(dev);
	struct gpio_dev_priv *uc_priv = dev_get_uclass_priv(dev);

	priv->regs = dev_remap_addr(dev);
	if (!priv->regs)
		return -EINVAL;

	priv->gpio_count = dev_read_u32_default(dev, "ngpios", 18);
	uc_priv->gpio_count = priv->gpio_count;
	uc_priv->bank_name = dev->name;

	return 0;
}

static const struct udevice_id ath79_gpio_ids[] = {
	{ .compatible = "qca,ath79-gpio" },
	{ }
};

U_BOOT_DRIVER(ath79_gpio) = {
	.name		= "ath79_gpio",
	.id		= UCLASS_GPIO,
	.of_match	= ath79_gpio_ids,
	.ops		= &ath79_gpio_ops,
	.probe		= ath79_gpio_probe,
	.priv_auto	= sizeof(struct ath79_gpio_priv),
};
