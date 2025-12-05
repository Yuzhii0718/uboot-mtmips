// SPDX-License-Identifier: GPL-2.0+

/* FILE NAME:  aeon_as21xx.c
 * PURPOSE:
 *      AS21XXX phy driver for uboot
 * NOTES:
 *
 */

/* INCLUDE FILE DECLARATIONS
 */
#include "aeon_as21xx.h"

static int aeon_ipc_send_cmd(struct phy_device *phydev,
				struct as21xxx_priv *priv,
				unsigned short cmd, unsigned short *ret_sts)
{
	bool curr_parity;
	int ret;
	unsigned int val;

	/* The IPC sync by using a single parity bit.
	 * Each CMD have alternately this bit set or clear
	 * to understand correct flow and packet order.
	 */
	curr_parity = priv->parity_status;
	if (priv->parity_status)
		cmd |= AEON_IPC_CMD_PARITY;

	/* Always update parity for next packet */
	priv->parity_status = !priv->parity_status;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_IPC_CMD, cmd);
	if (ret)
		return ret;

	/* Wait for packet to be processed */
	udelay(AEON_IPC_DELAY);

	/* With no ret_sts, ignore waiting for packet completion
	 * (ipc parity bit sync)
	 */
	if (!ret_sts)
		return 0;

	/* Exit condition logic:
	 * - Wait for parity bit equal
	 * - Wait for status success, error OR ready
	 */
	ret = read_poll_timeout(phy_read_mmd, val,
				(FIELD_GET(AEON_IPC_STS_PARITY, val) == curr_parity &&
				(val & AEON_IPC_STS_STATUS) != AEON_IPC_STS_STATUS_RCVD &&
				(val & AEON_IPC_STS_STATUS) != AEON_IPC_STS_STATUS_PROCESS &&
				(val & AEON_IPC_STS_STATUS) != AEON_IPC_STS_STATUS_BUSY) ||
				(val < 0), 10000, 2000000,
				phydev, MDIO_MMD_VEND1, VEND1_IPC_STS);

	if (val < 0)
		ret = val;

	if (ret)
		printf("%s fail to polling status failed: %d\n", __func__, ret);

	*ret_sts = val;
	if ((val & AEON_IPC_STS_STATUS) != AEON_IPC_STS_STATUS_SUCCESS)
		return -EFAULT;

	return 0;
}

static int aeon_ipc_send_msg(struct phy_device *phydev,
				unsigned short opcode, unsigned short *data, unsigned int data_len,
				unsigned short *ret_sts)
{
	struct as21xxx_priv *priv = (struct as21xxx_priv *)phydev->priv;
	unsigned short cmd;
	int ret;
	int i;

	/* IPC have a max of 8 register to transfer data,
	 * make sure we never exceed this.
	 */
	if (data_len > AEON_IPC_DATA_MAX)
		return -EINVAL;

	for (i = 0; i < data_len / sizeof(unsigned short); i++)
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_IPC_DATA(i),
				data[i]);

	cmd = FIELD_PREP(AEON_IPC_CMD_SIZE, data_len) |
		FIELD_PREP(AEON_IPC_CMD_OPCODE, opcode);
	ret = aeon_ipc_send_cmd(phydev, priv, cmd, ret_sts);
	if (ret)
		printf("failed to send ipc msg for %x: %d\n", opcode, ret);

	return ret;
}

static int aeon_ipc_rcv_msg(struct phy_device *phydev,
				unsigned short ret_sts, unsigned short *data)
{
	unsigned int size;
	int ret;
	int i;

	if ((ret_sts & AEON_IPC_STS_STATUS) == AEON_IPC_STS_STATUS_ERROR)
		return -EINVAL;

	/* Prevent IPC from stack smashing the kernel */
	size = FIELD_GET(AEON_IPC_STS_SIZE, ret_sts);
	if (size > AEON_IPC_DATA_MAX)
		return -EINVAL;

	for (i = 0; i < DIV_ROUND_UP(size, sizeof(unsigned short)); i++) {
		ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_IPC_DATA(i));
		if (ret < 0) {
			size = ret;
			goto out;
		}

		data[i] = ret;
	}

out:
	return size;
}

static int aeon_ipc_noop(struct phy_device *phydev,
			 struct as21xxx_priv *priv, unsigned short *ret_sts)
{
	unsigned short cmd;

	cmd = FIELD_PREP(AEON_IPC_CMD_SIZE, 0) |
		FIELD_PREP(AEON_IPC_CMD_OPCODE, IPC_CMD_NOOP);

	return aeon_ipc_send_cmd(phydev, priv, cmd, ret_sts);
}

/* Logic to sync parity bit with IPC.
 * We send 2 NOP cmd with same partity and we wait for IPC
 * to handle the packet only for the second one. This way
 * we make sure we are sync for every next cmd.
 */
static int aeon_ipc_sync_parity(struct phy_device *phydev)
{
	unsigned short ret_sts;
	int ret;
	struct as21xxx_priv *priv = (struct as21xxx_priv *)phydev->priv;

	/* Send NOP with no parity */
	aeon_ipc_noop(phydev, priv, NULL);

	/* Reset packet parity */
	priv->parity_status = false;

	/* Send second NOP with no parity */
	ret = aeon_ipc_noop(phydev, priv, &ret_sts);

	/* We expect to return -EINVAL */
	if (ret != -EFAULT)
		return ret;

	if ((ret_sts & AEON_IPC_STS_STATUS) != AEON_IPC_STS_STATUS_READY) {
		printf("Invalid IPC status on sync parity: %x\n", ret_sts);
		return -EINVAL;
	}

	return 0;
}

static int aeon_ipc_get_fw_version(struct phy_device *phydev)
{
	unsigned short ret_data[8], data[1];
	unsigned short ret_sts;
	int ret;

	data[0] = IPC_INFO_VERSION;
	ret = aeon_ipc_send_msg(phydev, IPC_CMD_INFO, data, sizeof(data), &ret_sts);
	if (ret)
		return ret;

	ret = aeon_ipc_rcv_msg(phydev, ret_sts, ret_data);
	if (ret < 0)
		return ret;

	debug("Firmware Version: %s\n", (char *)ret_data);

	return 0;
}

static int aeon_dpc_ra_enable(struct phy_device *phydev)
{
	unsigned short data[2];
	unsigned short ret_sts;

	data[0] = IPC_CFG_PARAM_DIRECT;
	data[1] = IPC_CFG_PARAM_DIRECT_DPC_RA;

	return aeon_ipc_send_msg(phydev, IPC_CMD_CFG_PARAM, data, sizeof(data), &ret_sts);
}

static int aeon_safety_load(struct phy_device *phydev)
{
	int retry = 0, write_len = 0, ret = 0, idx = 0;
	unsigned short *write_data = (unsigned short *)aeon_fw;

	while (retry < 5) {
		write_len = sizeof(aeon_fw);
		if (write_len == 0) {
			printf("AS21XXX no firmware exist.\r\n");
			return -1;
		}

		while (write_len > 0) {
			ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_MDIO_INDIRECT_LOAD, write_data[idx]);
			if (ret < 0) {
				printf("AS21XXX failed to write fw bin, retry load fw.\r\n" );
				break;
			}
			write_len -= 2;
			idx += 1;
		}
		if (write_len == 0)
			return 0;

		retry++;
		idx = 0;
	}

	return -1;
}

static int aeon_load_firmware(struct phy_device *phydev)
{
	int val = 0;

	// mdio boot set up
	val = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL); //GLB_REG_CPU_CTRL
	val &= 0xFFE5;
	val |= 0x16;
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL, val);
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_FW_START_ADDR, 0x1000); //set start addr of loading FW
	val = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_MDIO_INDIRECT_ADDRCMD); //GLB_REG_MDIO_INDIRECT_ADDRCMD
	val &= 0x3FFC;
	val |= 0xC000;
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_MDIO_INDIRECT_ADDRCMD, val);

	val = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_MDIO_INDIRECT_ADDRCMD);
	debug("AS21XXX MDIO_INDIRECT_ADDRCMD : %x\n", val);

	val = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_MDIO_INDIRECT_STATUS);
	if (val > 1) {
		printf("AS21XXX wrong origin mdio_indirect_status: %d\n", val);
		return -1;
	}

	debug("AS21XXX start to load fw bin, please waiting.....\r\n" );
	if (aeon_safety_load(phydev)) {
		printf("AS21XXX failed to load fw bin.\r\n" );
		return -1;
	}

	//aeon_mdio_trigger_boot
	phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x3, (AEON_BOOT_ADDR << 1) & 0xFFFF); // GLB_REG_CPU_RESET_ADDR_LO_BASEADDR
	phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x4, (AEON_BOOT_ADDR << 1) >> 16); // GLB_REG_CPU_RESET_ADDR_HI_BASEADDR

	val = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL); //GLB_REG_CPU_CTRL
	val &= 0xFFE1;
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL, val);

	return 0;
}

static int aeon_phy_config(struct phy_device *phydev)
{
	int val = 0;
	struct as21xxx_priv *priv;

	priv = malloc(sizeof(*priv));
	if (!priv)
		return -ENOMEM;
	phydev->priv = priv;

	// set ptp_clk (bit 6)
	val = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_PTP_CLK);
	val |= (1<<6);
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_PTP_CLK, val);
	val = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_PTP_CLK);

	if (aeon_load_firmware(phydev)) {
		printf("AS21XXX load firmware fail.\n");
		return -1;
	}

	if (aeon_ipc_sync_parity(phydev) && aeon_safety_load(phydev)) {
		printf("AS21XXX reload firmware fail.\r\n" );
		return -1;
	}

	aeon_ipc_get_fw_version(phydev);
	debug("AS21XXX initialize OK!\n");

	return aeon_dpc_ra_enable(phydev);
}

U_BOOT_PHY_DRIVER(AS21XXX) = {
	.name = "AS21XXX",
	.uid = AS21XXX_PHY_ID,
	.mask = 0xffffffff,
	.features = PHY_10G_FEATURES,
	.mmds = (MDIO_MMD_PMAPMD | MDIO_MMD_PCS |
		 MDIO_MMD_PHYXS | MDIO_MMD_AN |
		 MDIO_MMD_VEND1),
	.config = &aeon_phy_config,
	.startup = &genphy_update_link,
	.shutdown = &genphy_shutdown,
};