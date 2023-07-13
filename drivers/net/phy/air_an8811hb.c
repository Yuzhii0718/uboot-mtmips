// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for the Airoha AN8811HB 2.5 Gigabit PHY.
 *
 *
 * Copyright (C) 2025 Airoha Technology Corp.
 */
#include <phy.h>
#include <errno.h>
#include <malloc.h>
#include <fs.h>
#include <asm/unaligned.h>
#include <version.h>
#include <linux/compat.h>
#include <u-boot/crc.h>

#include "air_an8811hb_fw_crc.h"

#define AIR_UBOOT_REVISION ((((U_BOOT_VERSION_NUM / 1000) % 10) << 20) | \
			(((U_BOOT_VERSION_NUM / 100) % 10) << 16) | \
			(((U_BOOT_VERSION_NUM / 10) % 10) << 12) | \
			((U_BOOT_VERSION_NUM % 10) << 8) | \
			(((U_BOOT_VERSION_NUM_PATCH / 10) % 10) << 4) | \
			((U_BOOT_VERSION_NUM_PATCH % 10) << 0))

#if AIR_UBOOT_REVISION > 0x202003
#include <log.h>
#include <dm/device_compat.h>
#include <linux/iopoll.h>
#include <linux/delay.h>

#else
#include <common.h>

/* CL45 MDIO control */
#define MII_MMD_ACC_CTL_REG         0x0d
#define MII_MMD_ADDR_DATA_REG       0x0e
#define MMD_OP_MODE_DATA            BIT(14)
#define GENMASK(h, l) \
	(((~0UL) << (l)) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#define BIT(nr)			(1UL << (nr))
#endif

#define AN8811HB_PHY_ID		0xc0ff04a0

#define AN8811HB_DRIVER_VERSION  "v0.0.3"

#define AIR_FW_ADDR_DM	0x00000000
#define AIR_FW_ADDR_DSP	0x00100000

#define AIR_MD32_DM_SIZE   0x8000
#define AIR_MD32_DSP_SIZE  0x20000

/* MII Registers */
#define AIR_AUX_CTRL_STATUS		0x1d
#define   AIR_AUX_CTRL_STATUS_SPEED_MASK	GENMASK(4, 2)
#define   AIR_AUX_CTRL_STATUS_SPEED_10		0x0
#define   AIR_AUX_CTRL_STATUS_SPEED_100		0x4
#define   AIR_AUX_CTRL_STATUS_SPEED_1000	0x8
#define   AIR_AUX_CTRL_STATUS_SPEED_2500	0xc

#define AIR_EXT_PAGE_ACCESS		0x1f
#define AIR_PHY_PAGE_STANDARD		0x0000
#define AIR_PHY_PAGE_EXTENDED_4		0x0004

#define AIR_PBUS_MODE_ADDR_HIGH		0x1c
/* MII Registers Page 4*/
#define AIR_BPBUS_MODE			0x10
#define AIR_BPBUS_MODE_ADDR_FIXED	0x0000
#define AIR_BPBUS_MODE_ADDR_INCR	BIT(15)
#define AIR_BPBUS_WR_ADDR_HIGH		0x11
#define AIR_BPBUS_WR_ADDR_LOW		0x12
#define AIR_BPBUS_WR_DATA_HIGH		0x13
#define AIR_BPBUS_WR_DATA_LOW		0x14
#define AIR_BPBUS_RD_ADDR_HIGH		0x15
#define AIR_BPBUS_RD_ADDR_LOW		0x16
#define AIR_BPBUS_RD_DATA_HIGH		0x17
#define AIR_BPBUS_RD_DATA_LOW		0x18

/* Registers on MDIO_MMD_VEND1 */
#define AIR_PHY_FW_STATUS		0x8009
#define   AIR_PHY_READY			0x02

#define AIR_PHY_MCU_CMD_1		0x800c
#define AIR_PHY_MCU_CMD_1_MODE1			0x0
#define AIR_PHY_MCU_CMD_2		0x800d
#define AIR_PHY_MCU_CMD_2_MODE1			0x0
#define AIR_PHY_MCU_CMD_3		0x800e
#define AIR_PHY_MCU_CMD_3_MODE1			0x1101
#define AIR_PHY_MCU_CMD_3_DOCMD			0x1100
#define AIR_PHY_MCU_CMD_4		0x800f
#define AIR_PHY_MCU_CMD_4_MODE1			0x0002
#define AIR_PHY_MCU_CMD_4_INTCLR		0x00e4

/* Registers on MDIO_MMD_VEND2 */
#define AIR_PHY_LED_BCR			0x021
#define AIR_PHY_LED_BCR_MODE_MASK		GENMASK(1, 0)
#define AIR_PHY_LED_BCR_TIME_TEST	BIT(2)
#define AIR_PHY_LED_BCR_CLK_EN		BIT(3)
#define AIR_PHY_LED_BCR_EXT_CTRL	BIT(15)

#define AIR_PHY_LED_DUR_ON		0x022

#define AIR_PHY_LED_DUR_BLINK		0x023

#define AIR_PHY_LED_ON(i)	       (0x024 + ((i) * 2))
#define AIR_PHY_LED_ON_MASK		(GENMASK(6, 0) | BIT(8))
#define AIR_PHY_LED_ON_LINK1000		BIT(0)
#define AIR_PHY_LED_ON_LINK100		BIT(1)
#define AIR_PHY_LED_ON_LINK10		BIT(2)
#define AIR_PHY_LED_ON_LINKDOWN		BIT(3)
#define AIR_PHY_LED_ON_FDX		BIT(4) /* Full duplex */
#define AIR_PHY_LED_ON_HDX		BIT(5) /* Half duplex */
#define AIR_PHY_LED_ON_FORCE_ON		BIT(6)
#define AIR_PHY_LED_ON_LINK2500		BIT(8)
#define AIR_PHY_LED_ON_POLARITY		BIT(14)
#define AIR_PHY_LED_ON_ENABLE			BIT(15)

#define AIR_PHY_LED_BLINK(i)	       (0x025 + ((i) * 2))
#define AIR_PHY_LED_BLINK_1000TX	BIT(0)
#define AIR_PHY_LED_BLINK_1000RX	BIT(1)
#define AIR_PHY_LED_BLINK_100TX		BIT(2)
#define AIR_PHY_LED_BLINK_100RX		BIT(3)
#define AIR_PHY_LED_BLINK_10TX		BIT(4)
#define AIR_PHY_LED_BLINK_10RX		BIT(5)
#define AIR_PHY_LED_BLINK_COLLISION	BIT(6)
#define AIR_PHY_LED_BLINK_RX_CRC_ERR	BIT(7)
#define AIR_PHY_LED_BLINK_RX_IDLE_ERR	BIT(8)
#define AIR_PHY_LED_BLINK_FORCE_BLINK	BIT(9)
#define AIR_PHY_LED_BLINK_2500TX	BIT(10)
#define AIR_PHY_LED_BLINK_2500RX	BIT(11)

/* Registers on BUCKPBUS */
#define AIR_PHY_CONTROL			0x3a9c
#define   AIR_PHY_CONTROL_INTERNAL		BIT(11)

#define AIR_PHY_MD32FW_VERSION		0x3b3c

#define AN8811HB_GPIO_OUTPUT		0x5cf8b8
#define   AN8811HB_GPIO_OUTPUT_MASK		GENMASK(15, 0)
#define   AN8811HB_GPIO_OUTPUT_345		(BIT(3) | BIT(4) | BIT(5))
#define   AN8811HB_GPIO_OUTPUT_0115		(BIT(0) | BIT(1) | BIT(15))

#define AN8811HB_GPIO_SEL		0x5cf8bc
#define   AN8811HB_GPIO_SEL_0115_MASK		(GENMASK(31, 28) | GENMASK(7, 4) | GENMASK(3, 0))
#define   AN8811HB_GPIO_SEL_0			BIT(0)
#define   AN8811HB_GPIO_SEL_1			0
#define   AN8811HB_GPIO_SEL_15			BIT(29)

#define AN8811HB_CRC_PM_SET1		0xF020C
#define AN8811HB_CRC_PM_MON2		0xF0218
#define AN8811HB_CRC_PM_MON3		0xF021C
#define AN8811HB_CRC_DM_SET1		0xF0224
#define AN8811HB_CRC_DM_MON2		0xF0230
#define AN8811HB_CRC_DM_MON3		0xF0234
#define   AN8811HB_CRC_RD_EN			BIT(0)
#define   AN8811HB_CRC_ST			(BIT(0) | BIT(1))
#define   AN8811HB_CRC_CHECK_PASS		BIT(0)

#define AN8811HB_TX_POLARITY		0x5ce004
#define   AN8811HB_TX_POLARITY_NORMAL		BIT(7)
#define AN8811HB_RX_POLARITY		0x5ce61c
#define   AN8811HB_RX_POLARITY_NORMAL		BIT(7)

#define AN8811HB_HWTRAP1		0x5cf910
#define AN8811HB_HWTRAP2		0x5cf914
#define   AN8811HB_HWTRAP2_CKO			BIT(28)
#define   AN8811HB_HWTRAP2_PKG			(BIT(12) | BIT(13) | BIT(14))
#define AN8811HB_PRO_ID			0x5cf920
#define   AN8811HB_PRO_ID_VERSION		GENMASK(3, 0)

#define AN8811HB_CLK_DRV		0x5cf9e4
#define AN8811HB_CLK_DRV_CKO_MASK		GENMASK(14, 12)
#define   AN8811HB_CLK_DRV_CKOPWD		BIT(12)
#define   AN8811HB_CLK_DRV_CKO_LDPWD		BIT(13)
#define   AN8811HB_CLK_DRV_CKO_LPPWD		BIT(14)

#define AIR_PHY_FW_CTRL_1		0x0f0018
#define   AIR_PHY_FW_CTRL_1_START		0x0
#define   AIR_PHY_FW_CTRL_1_FINISH		0x1

#define air_upper_16_bits(n) ((u16)((n) >> 16))
#define air_lower_16_bits(n) ((u16)((n) & 0xffff))
#define clear_bit(bit, bitmap)	__clear_bit(bit, bitmap)

/* Led definitions */
#define AIR_PHY_LED_COUNT	3

/***** User defined *****/
/*****0:0R, 1:5R********/
#define AIR_SURGE_PROTECT 0

#define AN8811HB_PN_SWAP_TX		0
#define AN8811HB_PN_SWAP_RX		0

#define AIR_PHY_OUTPUT_CLOCK	0
/************************/

struct led {
	unsigned long rules;
	unsigned long state;
};

enum {
	AIR_PHY_LED_STATE_FORCE_ON,
	AIR_PHY_LED_STATE_FORCE_BLINK,
};

enum {
	AIR_PHY_LED_DUR_BLINK_32MS,
	AIR_PHY_LED_DUR_BLINK_64MS,
	AIR_PHY_LED_DUR_BLINK_128MS,
	AIR_PHY_LED_DUR_BLINK_256MS,
	AIR_PHY_LED_DUR_BLINK_512MS,
	AIR_PHY_LED_DUR_BLINK_1024MS,
};

enum {
	AIR_LED_DISABLE,
	AIR_LED_ENABLE,
};

enum {
	AIR_ACTIVE_LOW,
	AIR_ACTIVE_HIGH,
};

enum {
	AIR_LED_MODE_DISABLE,
	AIR_LED_MODE_USER_DEFINE,
};

/* Trigger specific enum */
enum air_led_trigger_netdev_modes {
	AIR_TRIGGER_NETDEV_LINK = 0,
	AIR_TRIGGER_NETDEV_LINK_10,
	AIR_TRIGGER_NETDEV_LINK_100,
	AIR_TRIGGER_NETDEV_LINK_1000,
	AIR_TRIGGER_NETDEV_LINK_2500,
	AIR_TRIGGER_NETDEV_LINK_5000,
	AIR_TRIGGER_NETDEV_LINK_10000,
	AIR_TRIGGER_NETDEV_HALF_DUPLEX,
	AIR_TRIGGER_NETDEV_FULL_DUPLEX,
	AIR_TRIGGER_NETDEV_TX,
	AIR_TRIGGER_NETDEV_RX,
	AIR_TRIGGER_NETDEV_TX_ERR,
	AIR_TRIGGER_NETDEV_RX_ERR,

	/* Keep last */
	__AIR_TRIGGER_NETDEV_MAX,
};

/* Default LED setup:
 * GPIO5 <-> LED0  On: Link detected, blink Rx/Tx
 * GPIO4 <-> LED1  On: Link detected at 2500 and 1000 Mbps
 * GPIO3 <-> LED2  On: Link detected at 2500 and  100 Mbps
 */
#define AIR_DEFAULT_TRIGGER_LED0 (BIT(AIR_TRIGGER_NETDEV_LINK)      | \
				  BIT(AIR_TRIGGER_NETDEV_RX)        | \
				  BIT(AIR_TRIGGER_NETDEV_TX))
#define AIR_DEFAULT_TRIGGER_LED1 (BIT(AIR_TRIGGER_NETDEV_LINK_2500) | \
				  BIT(AIR_TRIGGER_NETDEV_LINK_1000))
#define AIR_DEFAULT_TRIGGER_LED2 (BIT(AIR_TRIGGER_NETDEV_LINK_2500) | \
				  BIT(AIR_TRIGGER_NETDEV_LINK_100))

#define AIR_PHY_LED_DUR_UNIT	781
#define AIR_PHY_LED_DUR (AIR_PHY_LED_DUR_UNIT << AIR_PHY_LED_DUR_BLINK_64MS)

struct an8811hb_priv {
	u32		firmware_version;
	bool		mcu_needs_restart;
	struct led	led[AIR_PHY_LED_COUNT];
	unsigned int	pro_id;
	unsigned int	pkg_sel;
};

static int air_phy_read_page(struct phy_device *phydev)
{
	return phy_read(phydev, MDIO_DEVAD_NONE, AIR_EXT_PAGE_ACCESS);
}

static int air_phy_write_page(struct phy_device *phydev, int page)
{
	return phy_write(phydev, MDIO_DEVAD_NONE, AIR_EXT_PAGE_ACCESS, page);
}

#if AIR_UBOOT_REVISION < 0x201906
static int phy_read_mmd(struct phy_device *phydev, int devad, u16 reg)
{
    int ret = 0;
    int data;

    ret |= phy_write(phydev, MDIO_DEVAD_NONE, MII_MMD_ACC_CTL_REG, devad);
    ret |= phy_write(phydev, MDIO_DEVAD_NONE, MII_MMD_ADDR_DATA_REG, reg);
    ret |= phy_write(phydev, MDIO_DEVAD_NONE, MII_MMD_ACC_CTL_REG, MMD_OP_MODE_DATA | devad);
    if (ret < 0) {
        printf("phy_write, ret: %d\n", ret);
        return ret;
    }
    data = phy_read(phydev, MDIO_DEVAD_NONE, MII_MMD_ADDR_DATA_REG);
    return data;
}

static int phy_write_mmd(struct phy_device *phydev, int devad, u16 reg, u16 write_data)
{
    int ret = 0;

    ret |= phy_write(phydev, MDIO_DEVAD_NONE, MII_MMD_ACC_CTL_REG, devad);
    ret |= phy_write(phydev, MDIO_DEVAD_NONE, MII_MMD_ADDR_DATA_REG, reg);
    ret |= phy_write(phydev, MDIO_DEVAD_NONE, MII_MMD_ACC_CTL_REG, MMD_OP_MODE_DATA | devad);
    ret |= phy_write(phydev, MDIO_DEVAD_NONE, MII_MMD_ADDR_DATA_REG, write_data);
    if (ret < 0) {
        printf("phy_write, ret: %d\n", ret);
        return ret;
    }
    return 0;
}
#endif

int air_phy_select_page(struct phy_device *phydev, int page)
{
	int ret, oldpage;

	oldpage = air_phy_read_page(phydev);
	if (oldpage < 0)
		return oldpage;

	if (oldpage != page) {
		ret = air_phy_write_page(phydev, page);
		if (ret < 0)
			return ret;
	}

	return oldpage;
}

int air_phy_restore_page(struct phy_device *phydev, int oldpage, int ret)
{
	int r;

	if (oldpage < 0)
		return oldpage;

	r = air_phy_write_page(phydev, oldpage);
	if (ret >= 0 && r < 0)
		ret = r;

	return ret;
}

#if AIR_UBOOT_REVISION > 0x202003
static inline int air_phy_read_mmd_poll_timeout(struct phy_device *phydev,
						int devaddr, int regnum,
						int *val, int expected_val,
						unsigned int sleep_us,
						unsigned int timeout_us,
						bool sleep_before_read)
{
	int ret;
	int tmp_val;

	ret = read_poll_timeout(phy_read_mmd, tmp_val,
				(tmp_val == expected_val) || tmp_val < 0,
				sleep_us, timeout_us,
				phydev, devaddr, regnum);
	*val = tmp_val;
	if (tmp_val < 0)
		ret = tmp_val;
	if (ret)
		dev_err(phydev->dev, "%s failed: %d\n", __func__, ret);

	return ret;
}
#endif

int air_phy_modify_mmd_changed(struct phy_device *phydev, int devad, u32 regnum,
			   u16 mask, u16 set)
{
	int new, ret;

	ret = phy_read_mmd(phydev, devad, regnum);
	if (ret < 0)
		return ret;

	new = (ret & ~mask) | set;
	if (new == ret)
		return 0;

	ret = phy_write_mmd(phydev, devad, regnum, new);

	return ret < 0 ? ret : 1;
}

int air_phy_modify_mmd(struct phy_device *phydev, int devad, u32 regnum,
		   u16 mask, u16 set)
{
	int ret;

	ret = air_phy_modify_mmd_changed(phydev, devad, regnum, mask, set);

	return ret < 0 ? ret : 0;
}


static int air_pbus_reg_write(struct phy_device *phydev,
				u32 pbus_reg, u32 pbus_data)
{
	struct mii_dev *bus = phydev->bus;
	int pbus_addr = (phydev->addr) + 8;
	int ret;

	ret = bus->write(bus, pbus_addr, MDIO_DEVAD_NONE, AIR_EXT_PAGE_ACCESS,
			 (pbus_reg >> 16));
	if (ret < 0)
		return ret;

	ret = bus->write(bus, pbus_addr, MDIO_DEVAD_NONE, AIR_PBUS_MODE_ADDR_HIGH,
			 ((pbus_reg & GENMASK(15, 6)) >> 6));
	if (ret < 0)
		return ret;

	ret = bus->write(bus, pbus_addr, MDIO_DEVAD_NONE,
			 ((pbus_reg & GENMASK(5, 2)) >> 2),
			 (pbus_data & GENMASK(15, 0)));
	if (ret < 0)
		return ret;

	ret = bus->write(bus, pbus_addr, MDIO_DEVAD_NONE, 0x10,
			 ((pbus_data & GENMASK(31, 16)) >> 16));
	if (ret < 0)
		return ret;

	return 0;
}

static int air_buckpbus_reg_write(struct phy_device *phydev,
				  u32 pbus_address, u32 pbus_data)
{
	int ret, saved_page;

	saved_page = air_phy_select_page(phydev, AIR_PHY_PAGE_EXTENDED_4);
	if (saved_page < 0)
		return saved_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_MODE,
			AIR_BPBUS_MODE_ADDR_FIXED);
	if (ret < 0)
		goto restore_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_WR_ADDR_HIGH,
			air_upper_16_bits(pbus_address));
	if (ret < 0)
		goto restore_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_WR_ADDR_LOW,
			air_lower_16_bits(pbus_address));
	if (ret < 0)
		goto restore_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_WR_DATA_HIGH,
			air_upper_16_bits(pbus_data));
	if (ret < 0)
		goto restore_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_WR_DATA_LOW,
			air_lower_16_bits(pbus_data));
	if (ret < 0)
		goto restore_page;

restore_page:
	if (ret < 0)
		printf("%s 0x%08x failed: %d\n", __func__,
		       pbus_address, ret);

	return air_phy_restore_page(phydev, saved_page, ret);
}

static int air_buckpbus_reg_read(struct phy_device *phydev,
				 u32 pbus_address, u32 *pbus_data)
{
	int pbus_data_low, pbus_data_high;
	int ret = 0, saved_page;

	saved_page = air_phy_select_page(phydev, AIR_PHY_PAGE_EXTENDED_4);
	if (saved_page < 0)
		return saved_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_MODE,
			AIR_BPBUS_MODE_ADDR_FIXED);
	if (ret < 0)
		goto restore_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_RD_ADDR_HIGH,
			air_upper_16_bits(pbus_address));
	if (ret < 0)
		goto restore_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_RD_ADDR_LOW,
			air_lower_16_bits(pbus_address));
	if (ret < 0)
		goto restore_page;

	pbus_data_high = phy_read(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_RD_DATA_HIGH);
	if (pbus_data_high < 0) {
		ret = pbus_data_high;
		goto restore_page;
	}

	pbus_data_low = phy_read(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_RD_DATA_LOW);
	if (pbus_data_low < 0) {
		ret = pbus_data_low;
		goto restore_page;
	}

	*pbus_data = pbus_data_low | (pbus_data_high << 16);

restore_page:
	if (ret < 0)
		printf("%s 0x%08x failed: %d\n", __func__,
		       pbus_address, ret);

	return air_phy_restore_page(phydev, saved_page, ret);
}

static int air_buckpbus_reg_modify(struct phy_device *phydev,
				   u32 pbus_address, u32 mask, u32 set)
{
	int pbus_data_low, pbus_data_high;
	u32 pbus_data_old, pbus_data_new;
	int ret = 0, saved_page;

	saved_page = air_phy_select_page(phydev, AIR_PHY_PAGE_EXTENDED_4);
	if (saved_page < 0)
		return saved_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_MODE,
			AIR_BPBUS_MODE_ADDR_FIXED);
	if (ret < 0)
		goto restore_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_RD_ADDR_HIGH,
			air_upper_16_bits(pbus_address));
	if (ret < 0)
		goto restore_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_RD_ADDR_LOW,
			air_lower_16_bits(pbus_address));
	if (ret < 0)
		goto restore_page;

	pbus_data_high = phy_read(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_RD_DATA_HIGH);
	if (pbus_data_high < 0)
		goto restore_page;

	pbus_data_low = phy_read(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_RD_DATA_LOW);
	if (pbus_data_low < 0)
		goto restore_page;

	pbus_data_old = pbus_data_low | (pbus_data_high << 16);
	pbus_data_new = (pbus_data_old & ~mask) | set;
	if (pbus_data_new == pbus_data_old)
		goto restore_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_WR_ADDR_HIGH,
			air_upper_16_bits(pbus_address));
	if (ret < 0)
		goto restore_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_WR_ADDR_LOW,
			air_lower_16_bits(pbus_address));
	if (ret < 0)
		goto restore_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_WR_DATA_HIGH,
			air_upper_16_bits(pbus_data_new));
	if (ret < 0)
		goto restore_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_WR_DATA_LOW,
			air_lower_16_bits(pbus_data_new));
	if (ret < 0)
		goto restore_page;

restore_page:
	if (ret < 0)
		printf("%s 0x%08x failed: %d\n", __func__,
		       pbus_address, ret);

	return air_phy_restore_page(phydev, saved_page, ret);
}

static int air_write_buf(struct phy_device *phydev, unsigned long address,
			 unsigned long array_size, const unsigned char *buffer)
{
	unsigned int offset;
	int ret, saved_page;
	u16 val;

	saved_page = air_phy_select_page(phydev, AIR_PHY_PAGE_EXTENDED_4);
	if (saved_page < 0)
		return saved_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_MODE,
			AIR_BPBUS_MODE_ADDR_INCR);
	if (ret < 0)
		goto restore_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_WR_ADDR_HIGH,
			air_upper_16_bits(address));
	if (ret < 0)
		goto restore_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_WR_ADDR_LOW,
			air_lower_16_bits(address));
	if (ret < 0)
		goto restore_page;

	for (offset = 0; offset < array_size; offset += 4) {
		val = get_unaligned_le16(&buffer[offset + 2]);
		ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_WR_DATA_HIGH, val);
		if (ret < 0)
			goto restore_page;

		val = get_unaligned_le16(&buffer[offset]);
		ret = phy_write(phydev, MDIO_DEVAD_NONE, AIR_BPBUS_WR_DATA_LOW, val);
		if (ret < 0)
			goto restore_page;
	}

restore_page:
	if (ret < 0)
		printf("%s 0x%08lx failed: %d\n", __func__,
		       address, ret);

	return air_phy_restore_page(phydev, saved_page, ret);
}

static int an8811hb_wait_mcu_ready(struct phy_device *phydev)
{
	int reg_value = 0;

#if AIR_UBOOT_REVISION > 0x202003
	int ret = 0;
	/* Because of mdio-lock, may have to wait for multiple loads */
	ret = air_phy_read_mmd_poll_timeout(phydev, MDIO_MMD_VEND1,
					    AIR_PHY_FW_STATUS, &reg_value,
					    AIR_PHY_READY,
					    20000, 7500000, true);
	if (ret) {
		printf("MCU not ready: 0x%x\n", reg_value);
		return -ENODEV;
	}
#else
	int retry;
	u32 pbus_value;

	retry = 15;
	do {
		mdelay(300);
		reg_value = phy_read_mmd(phydev, MDIO_MMD_VEND1, AIR_PHY_FW_STATUS);
		if (reg_value < 0)
			return reg_value;

		if (AIR_PHY_READY == reg_value) {
			printf("AN8811HB PHY ready!\n");
			break;
		}

		air_buckpbus_reg_read(phydev, 0x3b3c, &pbus_value);
		if (0 == retry) {
			printf("AN8811HB PHY is not ready. (MD32 FW Status reg: 0x%x)\n", reg_value);
			air_buckpbus_reg_read(phydev, 0x3b3c, &pbus_value);
			printf("Check MD32 FW Version: %08x\n", pbus_value);
			printf("AN8811HB initialize fail!\n");
			return 0;
		}
	} while (retry--);

#endif
	return 0;
}

static int an8811hb_check_crc(struct phy_device *phydev, u32 set1,
			     u32 mon2, u32 mon3)
{
	int retry = 10;
	u32 pbus_value;
	int ret;

	/* Configure CRC */
	ret = air_buckpbus_reg_modify(phydev, set1,
				      AN8811HB_CRC_RD_EN,
				      AN8811HB_CRC_RD_EN);
	if (ret < 0)
		return ret;
	air_buckpbus_reg_read(phydev, set1, &pbus_value);
	debug("%d: reg 0x%x val 0x%u!\n", __LINE__, set1, pbus_value);

	do {
		mdelay(300);
		air_buckpbus_reg_read(phydev, mon2, &pbus_value);
		debug("%d: reg 0x%x val 0x%x!\n", __LINE__, mon2, pbus_value );
		if (pbus_value & AN8811HB_CRC_ST) {
			air_buckpbus_reg_read(phydev, mon3, &pbus_value);
			debug("%d: reg 0x%x val 0x%u!\n", __LINE__, mon3, pbus_value);

			if (pbus_value & AN8811HB_CRC_CHECK_PASS)
				printf("CRC Check PASS!\n");
			else
				printf("CRC Check FAIL!(0x%lu)\n",
					    pbus_value & AN8811HB_CRC_CHECK_PASS);

			break;
		}

		if (!retry) {
			printf("CRC Check is not ready.(Status %u)\n", pbus_value);
			return -ENODEV;
		}
	} while (--retry);

	ret = air_buckpbus_reg_modify(phydev, set1,
				      AN8811HB_CRC_RD_EN, 0);
	if (ret < 0)
		return ret;

	air_buckpbus_reg_read(phydev, set1, &pbus_value);
	debug("%d: reg 0x%x val 0x%u!\n", __LINE__, set1, pbus_value);

	return 0;
}

static int an8811hb_set_reg_state(struct phy_device *phydev, int state)
{
	u32 reg1_addr, reg1_val, reg2_addr, reg2_val;
	int ret;

	switch (state) {
	case 0:
		reg1_addr = 0x5cf9f8;
		reg1_val = 0x10000;
		reg2_addr = 0x5cf9fc;
		reg2_val = 0x0;
		break;
	case 1:
		reg1_addr = 0x5cf9fc;
		reg1_val = 0x10000;
		reg2_addr = 0x5cf9f8;
		reg2_val = 0x10001;
		break;
	default:
		return -EINVAL;
	}

	printf("%s: setting state %d\n", __func__, state);

	ret = air_pbus_reg_write(phydev, reg1_addr, reg1_val);
	if (ret < 0)
		return ret;

	ret = air_pbus_reg_write(phydev, reg2_addr, reg2_val);
	if (ret < 0)
		return ret;

	mdelay(50);

	return 0;
}

__weak int an8811hb_read_fw(void **addr)
{
	const unsigned char *dsp_bin = EthMD32_CRC_DSP_bin;
	const unsigned char *dm_bin = EthMD32_CRC_DM_bin;
	size_t dsp_bin_size = EthMD32_CRC_DSP_bin_size;
	size_t dm_bin_size = EthMD32_CRC_DM_bin_size;
	u32 ca_crc32;
	void *buffer;

	buffer = malloc(AIR_MD32_DM_SIZE + AIR_MD32_DSP_SIZE);
	if (!buffer) {
		printf("Failed to allocate memory for firmware\n");
		return -ENOMEM;
	}

	memcpy(buffer, dm_bin, dm_bin_size);
	ca_crc32 = crc32(0, (unsigned char *)buffer, dm_bin_size);
	printf("DM crc32 is 0x%x\n", ca_crc32);

	memcpy(buffer + dm_bin_size, dsp_bin, dsp_bin_size);
	ca_crc32 = crc32(0, (unsigned char *)buffer + dm_bin_size, dsp_bin_size);
	printf("DSP crc32 is 0x%x\n", ca_crc32);

	*addr = buffer;

	return 0;
}

static int an8811hb_load_firmware(struct phy_device *phydev)
{
	struct an8811hb_priv *priv = phydev->priv;
	int ret, retry = 10;
	void *buffer;
	u32 reg_val;

	ret = an8811hb_set_reg_state(phydev, 0);
	if (ret < 0)
		return ret;

	ret = an8811hb_set_reg_state(phydev, 1);
	if (ret < 0)
		return ret;

	ret = an8811hb_read_fw(&buffer);
	if (ret < 0)
		goto an8811hb_load_firmware_out;

	ret = air_buckpbus_reg_write(phydev, AIR_PHY_FW_CTRL_1,
				     AIR_PHY_FW_CTRL_1_START);
	if (ret < 0)
		goto an8811hb_load_firmware_out;

	ret = air_write_buf(phydev, AIR_FW_ADDR_DM, AIR_MD32_DM_SIZE,
			    (unsigned char *)buffer);
	if (ret < 0)
		goto an8811hb_load_firmware_out;

	ret = an8811hb_check_crc(phydev, AN8811HB_CRC_DM_SET1,
				AN8811HB_CRC_DM_MON2, AN8811HB_CRC_DM_MON3);
	if (ret < 0)
		goto an8811hb_load_firmware_out;

	ret = air_write_buf(phydev, AIR_FW_ADDR_DSP, AIR_MD32_DSP_SIZE,
			    (unsigned char *)buffer + AIR_MD32_DM_SIZE);
	if (ret < 0)
		goto an8811hb_load_firmware_out;

	ret = an8811hb_check_crc(phydev, AN8811HB_CRC_PM_SET1,
				AN8811HB_CRC_PM_MON2, AN8811HB_CRC_PM_MON3);
	if (ret < 0)
		goto an8811hb_load_firmware_out;

	ret = air_buckpbus_reg_write(phydev, AIR_PHY_FW_CTRL_1,
				     AIR_PHY_FW_CTRL_1_FINISH);
	if (ret < 0)
		goto an8811hb_load_firmware_out;

	do {
		mdelay(300);

		air_buckpbus_reg_read(phydev, AIR_PHY_FW_CTRL_1, &reg_val);
		if (reg_val == AIR_PHY_FW_CTRL_1_FINISH)
			break;

		printf("%d: reg 0x%x val 0x%x!\n",
		       __LINE__, AIR_PHY_FW_CTRL_1, reg_val);

		ret = air_buckpbus_reg_write(phydev, AIR_PHY_FW_CTRL_1,
					     AIR_PHY_FW_CTRL_1_FINISH);
		if (ret < 0)
			goto an8811hb_load_firmware_out;

	} while(--retry);

	ret = an8811hb_wait_mcu_ready(phydev);

	air_buckpbus_reg_read(phydev, AIR_PHY_MD32FW_VERSION,
			      &priv->firmware_version);
	printf("MD32 firmware version: %08x\n",
	       priv->firmware_version);

an8811hb_load_firmware_out:
	free(buffer);
	if (ret < 0)
		printf("Firmware loading failed: %d\n", ret);

	return ret;
}

static int an8811hb_surge_protect_cfg(struct phy_device *phydev)
{
	int ret = 0;

#if AIR_UBOOT_REVISION > 0x201706
	ofnode node = phy_get_ofnode(phydev);
	if (ofnode_read_bool(node, "airoha,surge-5r"))
#else
	if (AIR_SURGE_PROTECT)
#endif
	{
		ret |= phy_write_mmd(phydev, 0x1e, AIR_PHY_MCU_CMD_1, 0x0);
		ret |= phy_write_mmd(phydev, 0x1e, AIR_PHY_MCU_CMD_2, 0x0);
		ret |= phy_write_mmd(phydev, 0x1e, AIR_PHY_MCU_CMD_3, 0x1101);
		ret |= phy_write_mmd(phydev, 0x1e, AIR_PHY_MCU_CMD_4, 0x00b0);
		if (ret < 0)
			return ret;
		printf("Surge Protection mode - 5R\n");
	} else
		printf("Surge Protection mode - 0R\n");
	return ret;
}

int an8811hb_cko_cfg(struct phy_device *phydev)
{
	u32 pbus_value;
	int ret = 0;

#if AIR_UBOOT_REVISION > 0x201706
	ofnode node = phy_get_ofnode(phydev);
	if (!ofnode_read_bool(node, "airoha,phy-output-clock"))
#else
	if (!AIR_PHY_OUTPUT_CLOCK)
#endif
	{
		ret = air_buckpbus_reg_modify(phydev, AN8811HB_CLK_DRV,
					      AN8811HB_CLK_DRV_CKO_MASK,
					      AN8811HB_CLK_DRV_CKOPWD    |
					      AN8811HB_CLK_DRV_CKO_LDPWD |
					      AN8811HB_CLK_DRV_CKO_LPPWD);
		if (ret < 0)
			return ret;

		printf("CKO Output mode - Disabled\n");
	} else {
		air_buckpbus_reg_read(phydev, AN8811HB_HWTRAP2, &pbus_value);
		printf("CKO Output %dMHz - Enabled\n",
		       (pbus_value & AN8811HB_HWTRAP2_CKO) ? 50 : 25);
	}

	return ret;
}

static int an8811hb_restart_mcu(struct phy_device *phydev)
{
	int ret;

	ret = an8811hb_set_reg_state(phydev, 0);
	if (ret < 0)
		return ret;

	ret = an8811hb_set_reg_state(phydev, 1);
	if (ret < 0)
		return ret;

	ret = phy_write_mmd(phydev, 0x1e, 0x8009, 0x0);
	if (ret < 0)
		return ret;

	ret = air_buckpbus_reg_write(phydev, AIR_PHY_FW_CTRL_1,
				     AIR_PHY_FW_CTRL_1_START);
	if (ret < 0)
		return ret;

	return air_buckpbus_reg_write(phydev, AIR_PHY_FW_CTRL_1,
				      AIR_PHY_FW_CTRL_1_FINISH);
}

static int air_led_hw_control_set(struct phy_device *phydev, u8 index,
				  unsigned long rules)
{
	struct an8811hb_priv *priv = phydev->priv;
	u16 on = 0, blink = 0;
	int ret;

	if (index >= AIR_PHY_LED_COUNT)
		return -EINVAL;

	priv->led[index].rules = rules;

	if (rules & BIT(AIR_TRIGGER_NETDEV_FULL_DUPLEX))
		on |= AIR_PHY_LED_ON_FDX;

	if (rules & (BIT(AIR_TRIGGER_NETDEV_LINK_10) | BIT(AIR_TRIGGER_NETDEV_LINK)))
		on |= AIR_PHY_LED_ON_LINK10;

	if (rules & (BIT(AIR_TRIGGER_NETDEV_LINK_100) | BIT(AIR_TRIGGER_NETDEV_LINK)))
		on |= AIR_PHY_LED_ON_LINK100;

	if (rules & (BIT(AIR_TRIGGER_NETDEV_LINK_1000) | BIT(AIR_TRIGGER_NETDEV_LINK)))
		on |= AIR_PHY_LED_ON_LINK1000;

	if (rules & (BIT(AIR_TRIGGER_NETDEV_LINK_2500) | BIT(AIR_TRIGGER_NETDEV_LINK)))
		on |= AIR_PHY_LED_ON_LINK2500;

	if (rules & BIT(AIR_TRIGGER_NETDEV_RX)) {
		blink |= AIR_PHY_LED_BLINK_10RX   |
			 AIR_PHY_LED_BLINK_100RX  |
			 AIR_PHY_LED_BLINK_1000RX |
			 AIR_PHY_LED_BLINK_2500RX;
	}

	if (rules & BIT(AIR_TRIGGER_NETDEV_TX)) {
		blink |= AIR_PHY_LED_BLINK_10TX   |
			 AIR_PHY_LED_BLINK_100TX  |
			 AIR_PHY_LED_BLINK_1000TX |
			 AIR_PHY_LED_BLINK_2500TX;
	}

	if (blink || on) {
		/* switch hw-control on, so led-on and led-blink are off */
		clear_bit(AIR_PHY_LED_STATE_FORCE_ON,
			  &priv->led[index].state);
		clear_bit(AIR_PHY_LED_STATE_FORCE_BLINK,
			  &priv->led[index].state);
	} else {
		priv->led[index].rules = 0;
	}

	ret = air_phy_modify_mmd(phydev, MDIO_MMD_VEND2, AIR_PHY_LED_ON(index),
			     AIR_PHY_LED_ON_MASK, on);

	if (ret < 0)
		return ret;

	return phy_write_mmd(phydev, MDIO_MMD_VEND2, AIR_PHY_LED_BLINK(index),
			     blink);
};

static int air_led_init(struct phy_device *phydev, u8 index, u8 state, u8 pol)
{
	int val = 0;
	int err;

	if (index >= AIR_PHY_LED_COUNT)
		return -EINVAL;

	if (state == AIR_LED_ENABLE)
		val |= AIR_PHY_LED_ON_ENABLE;
	else
		val &= ~AIR_PHY_LED_ON_ENABLE;

	if (pol == AIR_ACTIVE_HIGH)
		val |= AIR_PHY_LED_ON_POLARITY;
	else
		val &= ~AIR_PHY_LED_ON_POLARITY;

	err = phy_write_mmd(phydev, 0x1f, AIR_PHY_LED_ON(index), val);
	if (err < 0)
		return err;

	return 0;
}

/**
 * air_leds_init - Initialize and configure LEDs for a phy device.
 *
 * @phydev: Pointer to the phy_device structure.
 * @num: Number of LEDs to initialize.
 * @dur: Duration for LED blink in milliseconds. It sets the duration
 *       for both the ON and OFF periods (OFF period will be half of `dur`).
 * @mode: LED operation mode. Supported modes are:
 *           - AIR_LED_MODE_DISABLE: Disables LED control.
 *           - AIR_LED_MODE_USER_DEFINE: Enables user-defined LED control.
 *
 * Initializes and configures LEDs on a phy device with a specified blink duration
 * and mode. Supports disabling or enabling user-defined control.
 * Return:
 * On success, returns 0. On error, it returns a negative value that denotes
 * the error code.
 */

static int air_leds_init(struct phy_device *phydev, int num, u16 dur, int mode)
{
	struct an8811hb_priv *priv = phydev->priv;
	int ret, i;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, AIR_PHY_LED_DUR_BLINK,
			    dur);
	if (ret < 0)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, AIR_PHY_LED_DUR_ON,
			    dur >> 1);
	if (ret < 0)
		return ret;

	switch (mode) {
	case AIR_LED_MODE_DISABLE:
		ret = air_phy_modify_mmd(phydev, MDIO_MMD_VEND2, AIR_PHY_LED_BCR,
				     AIR_PHY_LED_BCR_EXT_CTRL |
				     AIR_PHY_LED_BCR_MODE_MASK, 0);
		break;
	case AIR_LED_MODE_USER_DEFINE:
		ret = air_phy_modify_mmd(phydev, MDIO_MMD_VEND2, AIR_PHY_LED_BCR,
				     AIR_PHY_LED_BCR_EXT_CTRL |
				     AIR_PHY_LED_BCR_CLK_EN,
				     AIR_PHY_LED_BCR_EXT_CTRL |
				     AIR_PHY_LED_BCR_CLK_EN);
		if (ret < 0)
			return ret;
		break;
	default:
		printf("LED mode %d is not supported\n", mode);
		return -EINVAL;
	}

	for (i = 0; i < num; ++i) {
		ret = air_led_init(phydev, i, AIR_LED_ENABLE, AIR_ACTIVE_HIGH);
		if (ret < 0) {
			printf("LED%d init failed: %d\n", i, ret);
			return ret;
		}
		air_led_hw_control_set(phydev, i, priv->led[i].rules);
	}

	return 0;
}

static int an8811hb_config(struct phy_device *phydev)
{
	struct an8811hb_priv *priv = phydev->priv;
	u32 pbus_value = 0;
	int ret = 0;

#if AIR_UBOOT_REVISION > 0x201706
	ofnode node;
	node = phy_get_ofnode(phydev);
	if (!ofnode_valid(node))
		return 0;
#endif

	/* If restart happened in .probe(), no need to restart now */
	if (priv->mcu_needs_restart) {
		ret = an8811hb_restart_mcu(phydev);
		if (ret < 0)
			return ret;
	} else {
		ret = an8811hb_load_firmware(phydev);
		if (ret) {
			printf("Load firmware fail.\n");
			return ret;
		}
		/* Next calls to .config() mcu needs to restart */
		priv->mcu_needs_restart = true;
	}


	ret = air_buckpbus_reg_read(phydev, AN8811HB_PRO_ID, &pbus_value);
	if (ret < 0)
		return ret;
	priv->pro_id = (pbus_value & AN8811HB_PRO_ID_VERSION) + 1;

	ret = air_buckpbus_reg_read(phydev, AN8811HB_HWTRAP2, &pbus_value);
	if (ret < 0)
		return ret;
	priv->pkg_sel = (pbus_value & AN8811HB_HWTRAP2_PKG) >> 12;

	printf("%s(%d) Version: E%d\n",
	       priv->pkg_sel ? "AN8811HBCN" : "AN8811HBN",
	       priv->pkg_sel, priv->pro_id);

	/* Serdes polarity */
	pbus_value = 0;
#if AIR_UBOOT_REVISION > 0x201706
	if (ofnode_read_bool(node, "airoha,pnswap-rx"))
#else
	if (AN8811HB_PN_SWAP_RX)
#endif
		pbus_value &= ~AN8811HB_RX_POLARITY_NORMAL;
	else
		pbus_value |=  AN8811HB_RX_POLARITY_NORMAL;

	debug("1 pbus_value 0x%x\n", pbus_value);
	ret = air_buckpbus_reg_modify(phydev, AN8811HB_RX_POLARITY,
				      AN8811HB_RX_POLARITY_NORMAL,
				      pbus_value);
	if (ret < 0)
		return ret;

	pbus_value = 0;
#if AIR_UBOOT_REVISION > 0x201706
	if (ofnode_read_bool(node, "airoha,pnswap-tx"))
#else
	if (AN8811HB_PN_SWAP_TX)
#endif
		pbus_value &= ~AN8811HB_TX_POLARITY_NORMAL;
	else
		pbus_value |=  AN8811HB_TX_POLARITY_NORMAL;

	debug("2 pbus_value 0x%x\n", pbus_value);
	ret = air_buckpbus_reg_modify(phydev, AN8811HB_TX_POLARITY,
				      AN8811HB_TX_POLARITY_NORMAL,
				      pbus_value);
	if (ret < 0)
		return ret;

	/* Configure led gpio pins as output */
	if (priv->pkg_sel) {
		ret = air_buckpbus_reg_modify(phydev, AN8811HB_GPIO_OUTPUT,
					      AN8811HB_GPIO_OUTPUT_MASK,
					      AN8811HB_GPIO_OUTPUT_0115);
		if (ret < 0)
			return ret;
		ret = air_buckpbus_reg_modify(phydev, AN8811HB_GPIO_SEL,
					      AN8811HB_GPIO_SEL_0115_MASK,
					      AN8811HB_GPIO_SEL_0 |
					      AN8811HB_GPIO_SEL_1 |
					      AN8811HB_GPIO_SEL_15);
		if (ret < 0)
			return ret;
	} else {
		ret = air_buckpbus_reg_modify(phydev, AN8811HB_GPIO_OUTPUT,
					      AN8811HB_GPIO_OUTPUT_345,
					      AN8811HB_GPIO_OUTPUT_345);
		if (ret < 0)
			return ret;
	}

	ret = air_leds_init(phydev, AIR_PHY_LED_COUNT, AIR_PHY_LED_DUR,
			    AIR_LED_MODE_USER_DEFINE);
	if (ret < 0) {
		printf("Failed to disable leds: %d\n", ret);
		return ret;
	}

	ret = an8811hb_surge_protect_cfg(phydev);
	if (ret < 0)
		printf("an8811hb_surge_protect_cfg fail. (ret=%d)\n", ret);

	/* Co-Clock Output */
	ret = an8811hb_cko_cfg(phydev);
	if (ret)
		return ret;

	printf("AN8811HB initialize OK ! (%s)\n", AN8811HB_DRIVER_VERSION);

	return 0;
}

static int an8811hb_update_duplex(struct phy_device *phydev)
{
	int lpa;

	if (phydev->autoneg == AUTONEG_ENABLE) {
		lpa = phy_read(phydev, MDIO_DEVAD_NONE, MII_LPA);
		if (lpa < 0)
			return lpa;

		switch (phydev->speed) {
		case SPEED_2500:
		case SPEED_1000:
			phydev->duplex = DUPLEX_FULL;
			break;
		case SPEED_100:
			phydev->duplex = (lpa & LPA_100FULL) ?
					  DUPLEX_FULL : DUPLEX_HALF;
			break;
		case SPEED_10:
			phydev->duplex = (lpa & LPA_10FULL) ?
					  DUPLEX_FULL : DUPLEX_HALF;
			break;
		}
	} else if (phydev->autoneg == AUTONEG_DISABLE) {
		u32 bmcr = phy_read(phydev, MDIO_DEVAD_NONE, MII_BMCR);

		if (phydev->speed == SPEED_2500)
			phydev->duplex = DUPLEX_FULL;
		else
			phydev->duplex = (bmcr & BMCR_FULLDPLX) ?
					  DUPLEX_FULL : DUPLEX_HALF;
	}

	return 0;
}

static int an8811hb_parse_status(struct phy_device *phydev)
{
	int ret = 0, reg_value;

	reg_value = phy_read(phydev, MDIO_DEVAD_NONE, AIR_AUX_CTRL_STATUS);
	if (reg_value < 0)
		return reg_value;

	switch (reg_value & AIR_AUX_CTRL_STATUS_SPEED_MASK) {
	case AIR_AUX_CTRL_STATUS_SPEED_2500:
		phydev->speed = SPEED_2500;
		break;
	case AIR_AUX_CTRL_STATUS_SPEED_1000:
		phydev->speed = SPEED_1000;
		break;
	case AIR_AUX_CTRL_STATUS_SPEED_100:
		phydev->speed = SPEED_100;
		break;
	case AIR_AUX_CTRL_STATUS_SPEED_10:
		phydev->speed = SPEED_10;
		break;
	default:
		printf("Auto-neg error, defaulting to 2500M/FD\n");
		phydev->speed = SPEED_2500;
		phydev->duplex = DUPLEX_FULL;
		return 0;
	}

	/* Update duplex mode based on speed and negotiation status */
	ret = an8811hb_update_duplex(phydev);
	if (ret < 0)
		return ret;

	return ret;
}

static int an8811hb_startup(struct phy_device *phydev)
{
	int ret = 0;

	ret = genphy_update_link(phydev);
	if (ret)
		return ret;

	return an8811hb_parse_status(phydev);
}

static int an8811hb_probe(struct phy_device *phydev)
{
	struct an8811hb_priv *priv;
	int phy_id;

	priv = malloc(sizeof(*priv));
	if (!priv)
		return -ENOMEM;
	memset(priv, 0, sizeof(*priv));

	phy_id = phy_read(phydev, MDIO_DEVAD_NONE, MII_PHYSID1) << 16;
	phy_id |= phy_read(phydev, MDIO_DEVAD_NONE, MII_PHYSID2);
	if (phy_id != AN8811HB_PHY_ID) {
		printf("AN8811HB can't be detected(0x%x).\n", phy_id);
		return 0;
	}

	priv->led[0].rules = AIR_DEFAULT_TRIGGER_LED0;
	priv->led[1].rules = AIR_DEFAULT_TRIGGER_LED1;
	priv->led[2].rules = AIR_DEFAULT_TRIGGER_LED2;

	/* mcu has just restarted after firmware load */
	priv->mcu_needs_restart = false;

	phydev->priv = priv;

	return 0;
}

#if AIR_UBOOT_REVISION > 0x202306
U_BOOT_PHY_DRIVER(an8811hb) = {
	.name = "Airoha AN8811HB",
	.uid = AN8811HB_PHY_ID,
	.mask = 0x0ffffff0,
	.config = &an8811hb_config,
	.probe = &an8811hb_probe,
	.startup = &an8811hb_startup,
	.shutdown = &genphy_shutdown,
};
#else
static struct phy_driver AIR_AN8811HB_driver = {
    .name = "Airoha AN8811HB",
    .uid = AN8811HB_PHY_ID,
    .mask = 0x0ffffff0,
	.config = &an8811hb_config,
	.probe = &an8811hb_probe,
	.startup = &an8811hb_startup,
	.shutdown = &genphy_shutdown,
};

int phy_air_an8811hb_init(void)
{
    phy_register(&AIR_AN8811HB_driver);
    return 0;
}
#endif
