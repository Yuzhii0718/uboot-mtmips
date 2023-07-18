# SPDX-License-Identifier:	GPL-2.0

obj-y	+= mt7988_rfb.o
obj-$(CONFIG_PHY_MEDIATEK_2P5GE) += i2p5ge_fw.o

# Copy firmware files to build directory before assembly (only if out-of-tree build)
$(obj)/i2p5ge_fw.o: copy_i2p5ge_fw

ifdef CONFIG_XZ
copy_i2p5ge_fw: $(srctree)/mt7988-i2p5ge-phy-pmb.xz
	@[ "$(srctree)" != "$(objtree)" ] && cp $< $(objtree)/ || true
else
copy_i2p5ge_fw: $(srctree)/mt7988-i2p5ge-phy-pmb.bin
	@[ "$(srctree)" != "$(objtree)" ] && cp $< $(objtree)/ || true
endif

.PHONY: copy_i2p5ge_fw
