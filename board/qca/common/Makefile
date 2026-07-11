# SPDX-License-Identifier: GPL-2.0

ifndef CONFIG_XPL_BUILD
ifdef CONFIG_QCA_BOOTMENU_LEGACY
obj-$(CONFIG_WEBUI_FAILSAFE) += qca_failsafe_legacy.o
obj-$(CONFIG_QCA_BOOTMENU) += cmd_qcaautoboot.o qca_bootmenu_legacy.o \
			      cmd_qcaboardboot.o cmd_qcaupgrade.o
else
obj-$(CONFIG_WEBUI_FAILSAFE) += qca_failsafe.o
obj-$(CONFIG_QCA_BOOTMENU) += cmd_qcaautoboot.o qca_bootmenu.o \
			      cmd_qcaboardboot.o cmd_qcaupgrade.o
endif
endif
