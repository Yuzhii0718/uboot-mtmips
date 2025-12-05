#ifndef __AEON_AS21XX_H
#define __AEON_AS21XX_H

#include <phy.h>
#include <linux/mtd/mtd.h>
#include <linux/bitfield.h>
#include <linux/iopoll.h>
#include "aeon_fw.h"

struct as21xxx_priv {
	bool parity_status;
};

#ifndef	BIT
#define	BIT(nr)	(1UL << (nr))
#endif
#ifndef	FIELD_PREP_CONST
#define	FIELD_PREP_CONST(mask, val)	(((typeof(mask))(val) << __bf_shf(mask)) & (mask))
#endif

#define	AEON_BOOT_ADDR	(0X2000 >> 1)
#define	AS21XXX_PHY_NUM	1

#define	AS21XXX_PHY_ID1	0x7500
#define	AS21XXX_PHY_ID2	0x9410
#define	AS21XXX_PHY_ID	((AS21XXX_PHY_ID1 << 16) | AS21XXX_PHY_ID2)
#define	AEON_MAX_LDES	5
#define	AEON_IPC_DELAY	15000
#define	AEON_IPC_TIMEOUT	(AEON_IPC_DELAY * 100)
#define	AEON_IPC_DATA_MAX	(8 * sizeof(unsigned short))

#define	AEON_IPC_CMD_PARITY	BIT(15)
#define	AEON_IPC_CMD_SIZE	GENMASK(10, 6)
#define	AEON_IPC_CMD_OPCODE	GENMASK(5, 0)

#define	IPC_CMD_NOOP	0x0  /* Do nothing */
#define	IPC_CMD_INFO	0x1  /* Get Firmware Version */
#define	IPC_CMD_SYS_CPU	0x2  /* SYS_CPU */
#define	IPC_CMD_BULK_DATA	0xa  /* Pass bulk data in ipc registers. */
#define	IPC_CMD_BULK_WRITE	0xc  /* Write bulk data to memory */
#define	IPC_CMD_CFG_PARAM	0x1a /* Write config parameters to memory */
#define	IPC_CMD_NG_TESTMODE	0x1b /* Set NG test mode and tone */
#define	IPC_CMD_TEMP_MON	0x15 /* Temperature monitoring function */
#define	IPC_CMD_SET_LED	0x23 /* Set led */

#define	VEND1_IPC_CMD	0x5801
#define	VEND1_IPC_STS	0x5802
#define	VEND1_IPC_DATA0	0x5808
#define	VEND1_IPC_DATA1	0x5809
#define	VEND1_IPC_DATA2	0x580a
#define	VEND1_IPC_DATA3	0x580b
#define	VEND1_IPC_DATA4	0x580c
#define	VEND1_IPC_DATA5	0x580d
#define	VEND1_IPC_DATA6	0x580e
#define	VEND1_IPC_DATA7	0x580f
#define	VEND1_IPC_DATA(_n)	(VEND1_IPC_DATA0 + (_n))

#define	VEND1_GLB_REG_CPU_CTRL	0xe
#define	VEND1_GLB_CPU_CTRL_MASK	GENMASK(4, 0)
#define	VEND1_GLB_CPU_CTRL_LED_POLARITY_MASK	GENMASK(12, 8)
#define	VEND1_GLB_CPU_CTRL_LED_POLARITY(_n)	FIELD_PREP(VEND1_GLB_CPU_CTRL_LED_POLARITY_MASK, \
							 BIT(_n))
#define	VEND1_FW_START_ADDR	0x100
#define	VEND1_GLB_REG_MDIO_INDIRECT_ADDRCMD	0x101
#define	VEND1_GLB_REG_MDIO_INDIRECT_LOAD	0x102
#define	VEND1_GLB_REG_MDIO_INDIRECT_STATUS	0x103
#define	VEND1_PTP_CLK	0x142
#define	VEND1_PTP_CLK_EN	BIT(6)

#define	AEON_IPC_STS_PARITY	BIT(15)
#define	AEON_IPC_STS_SIZE	GENMASK(14, 10)
#define	AEON_IPC_STS_OPCODE	GENMASK(9, 4)
#define	AEON_IPC_STS_STATUS	GENMASK(3, 0)
#define	AEON_IPC_STS_STATUS_RCVD	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0x1)
#define	AEON_IPC_STS_STATUS_PROCESS	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0x2)
#define	AEON_IPC_STS_STATUS_SUCCESS	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0x4)
#define	AEON_IPC_STS_STATUS_ERROR	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0x8)
#define	AEON_IPC_STS_STATUS_BUSY	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0xe)
#define	AEON_IPC_STS_STATUS_READY	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0xf)

/* Sub command of CMD_INFO */
#define	IPC_INFO_VERSION	0x1

/* Sub command of CMD_CFG_PARAM */
#define	IPC_CFG_PARAM_DIRECT	0x4

/* CFG DIRECT sub command */
#define	IPC_CFG_PARAM_DIRECT_NG_PHYCTRL	0x1
#define	IPC_CFG_PARAM_DIRECT_CU_AN	0x2
#define	IPC_CFG_PARAM_DIRECT_SDS_PCS	0x3
#define	IPC_CFG_PARAM_DIRECT_AUTO_EEE	0x4
#define	IPC_CFG_PARAM_DIRECT_SDS_PMA	0x5
#define	IPC_CFG_PARAM_DIRECT_DPC_RA	0x6
#define	IPC_CFG_PARAM_DIRECT_DPC_PKT_CHK	0x7
#define	IPC_CFG_PARAM_DIRECT_DPC_SDS_WAIT_ETH	0x8
#define	IPC_CFG_PARAM_DIRECT_WDT	0x9
#define	IPC_CFG_PARAM_DIRECT_SDS_RESTART_AN	0x10
#define	IPC_CFG_PARAM_DIRECT_TEMP_MON	0x11
#define	IPC_CFG_PARAM_DIRECT_WOL	0x12

#endif /* End of __as21xx_H */