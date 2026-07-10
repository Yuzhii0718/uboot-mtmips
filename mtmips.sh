#!/bin/sh
# ============================================================================
# mtmips.sh - Build U-Boot for MediaTek MTMIPS platform
#             (MT7620 / MT7621 / MT7628 & MT7688)
#
# Usage:
#   SOC=<mt7620|mt7621|mt7628|mt7688> BOARD=<board_name> ./mtmips.sh
#
# Examples:
#   SOC=mt7620 BOARD=rfb                 ./mtmips.sh
#   SOC=mt7621 BOARD=nmbm_rfb           ./mtmips.sh
#   SOC=mt7628 BOARD=linkit-smart-7688  ./mtmips.sh
#   SOC=mt7688 BOARD=linkit-smart-7688  ./mtmips.sh
#
# Note: MT7628 and MT7688 share the same toolchain (ramips/mt76x8)
#       and use "mt7628_" as the defconfig prefix.
#       Output directory/file use the actual SOC name passed by user.
#
# Environment variables:
#   SOC           Target SoC (required: mt7620, mt7621, mt7628, or mt7688)
#   BOARD         Target board name (required)
#   TOOLCHAIN     Cross-compiler prefix (auto-detected if empty)
#   JOBS          Parallel make jobs (default: nproc)
#   STAGING_DIR   Staging directory (passed to make)
#
# Output:
#   output_<soc>/<soc>-u-boot-<board>.bin
#
# Note: Toolchain should be placed in the parent directory (sibling of u-boot),
#       NOT inside the u-boot source tree. This avoids LTO plugin issues that
#       occur when the toolchain is a subdirectory of the build tree.
# ============================================================================

set -e

# ---------------------------------------------------------------------------
# --help / -h
# ---------------------------------------------------------------------------
show_help() {
	cat <<'EOF'
Usage: SOC=<mt7620|mt7621|mt7628|mt7688> BOARD=<board_name> [OPTIONS] ./mtmips.sh

Build U-Boot for MediaTek MTMIPS (MT7620 / MT7621 / MT7628 & MT7688) platform.

MT7628 and MT7688 share the same toolchain (ramips/mt76x8) and use
"mt7628_" as the defconfig prefix. Output uses the actual SOC name.

Required:
  SOC=<mt7620|mt7621|mt7628|mt7688>   Target SoC
  BOARD=<board>                       Target board name

Options:
  TOOLCHAIN=...   Cross-compiler prefix (auto-detected from ../openwrt*/toolchain-mipsel*)
  JOBS=<n>        Parallel make jobs (default: nproc)
  STAGING_DIR=... Staging directory (auto-detected from TOOLCHAIN)

Examples:
  SOC=mt7620 BOARD=rfb                 ./mtmips.sh
  SOC=mt7621 BOARD=nmbm_rfb           ./mtmips.sh
  SOC=mt7621 BOARD=nand_ax_rfb        ./mtmips.sh
  SOC=mt7628 BOARD=linkit-smart-7688  ./mtmips.sh
  SOC=mt7628 BOARD=rfb                ./mtmips.sh
  SOC=mt7688 BOARD=linkit-smart-7688  ./mtmips.sh
EOF
}

case "${1:-}" in
	--help|-h|help)
		show_help
		exit 0
		;;
esac

# ---------------------------------------------------------------------------
# Validate SOC
# ---------------------------------------------------------------------------
if [ -z "$SOC" ]; then
	echo "Usage: SOC=<mt7620|mt7621|mt7628|mt7688> BOARD=<board_name> $0"
	echo "Try '$0 --help' for more information."
	exit 1
fi

case "$SOC" in
	mt7620)
		TOOLCHAIN_SUBPATH="ramips/mt7620"
		TOOLCHAIN_PATTERN="openwrt*mt7620*"
		SOC_ID="mt7620"
		SOC_DEFCONFIG="mt7620"
		;;
	mt7621)
		TOOLCHAIN_SUBPATH="ramips/mt7621"
		TOOLCHAIN_PATTERN="openwrt*mt7621*"
		SOC_ID="mt7621"
		SOC_DEFCONFIG="mt7621"
		;;
	mt7628|mt7688)
		TOOLCHAIN_SUBPATH="ramips/mt76x8"
		TOOLCHAIN_PATTERN="openwrt*mt76x8*"
		SOC_ID="$SOC"
		SOC_DEFCONFIG="mt7628"
		;;
	*)
		echo "Error: Unsupported SOC='$SOC'. Valid values: mt7620, mt7621, mt7628, mt7688"
		exit 1
		;;
esac

if [ -z "$BOARD" ]; then
	echo "Usage: SOC=${SOC} BOARD=<board_name> $0"
	echo "Try '$0 --help' for more information."
	exit 1
fi

UBOOT_DIR=.
OUTPUT_DIR="output_mtmips"

die()
{
	echo "Error: $*"
	exit 1
}

# Read a CONFIG_ value from the generated .config
get_config()
{
	grep -oP "^CONFIG_$1=\K.*" "$UBOOT_DIR/.config" 2>/dev/null || true
}

# URL of the prebuilt OpenWrt toolchain (can be overridden by env)
TOOLCHAIN_URL_NAME=$(echo "$TOOLCHAIN_SUBPATH" | tr '/' '-')
TOOLCHAIN_URL="${TOOLCHAIN_URL:-https://downloads.openwrt.org/releases/25.12.5/targets/${TOOLCHAIN_SUBPATH}/openwrt-toolchain-25.12.5-${TOOLCHAIN_URL_NAME}_gcc-14.3.0_musl.Linux-x86_64.tar.zst}"

# ---------------------------------------------------------------------------
# Auto-detect or download toolchain (look in parent directory, not U-Boot tree)
# ---------------------------------------------------------------------------
PARENT_DIR="$(cd "$UBOOT_DIR/.."; pwd)"

find_toolchain() {
	TOOLCHAIN_BIN=""
	for dir in $PARENT_DIR/$TOOLCHAIN_PATTERN/toolchain-mipsel*/bin; do
		if [ -d "$dir" ]; then
			TOOLCHAIN_BIN=$(cd "$dir" && pwd)
			return 0
		fi
	done
	return 1
}

download_toolchain() {
	echo "Downloading toolchain from: $TOOLCHAIN_URL"
	cd "$PARENT_DIR" || return 1
	if command -v wget >/dev/null 2>&1; then
		wget -O - "$TOOLCHAIN_URL" | tar --zstd -xf - || return 1
	elif command -v curl >/dev/null 2>&1; then
		curl -L "$TOOLCHAIN_URL" | tar --zstd -xf - || return 1
	else
		echo "Neither wget nor curl found. Install one or download manually: $TOOLCHAIN_URL"
		return 1
	fi
	cd "$UBOOT_DIR" || return 1
}

if [ -z "$TOOLCHAIN" ]; then
	if ! find_toolchain; then
		echo "Toolchain not found in parent directory ($PARENT_DIR)."
		read -p "Download it now? [Y/n] " dlcc
		dlcc=${dlcc:-Y}
		case "$dlcc" in
			[Yy]* )
				download_toolchain || die "Toolchain download failed."
				find_toolchain || die "Toolchain not found after extraction."
				;;
			* )
				die "Toolchain required. Set TOOLCHAIN=... or place ${TOOLCHAIN_PATTERN}/toolchain-mipsel*/ in $PARENT_DIR."
				;;
		esac
	fi
	TOOLCHAIN="${TOOLCHAIN_BIN}/mipsel-openwrt-linux-"
fi

if [ -z "$STAGING_DIR" ]; then
	STAGING_DIR="${TOOLCHAIN_BIN%/bin}"
fi

# ---------------------------------------------------------------------------
# Build config
# ---------------------------------------------------------------------------
UBOOT_CFG="${SOC_DEFCONFIG}_${BOARD}_defconfig"

# ---------------------------------------------------------------------------
# Environment checks
# ---------------------------------------------------------------------------
echo "======================================================================"
echo "MTMIPS U-Boot Build (SOC=${SOC}, family=${SOC_ID})"
echo "======================================================================"

command -v python3 >/dev/null 2>&1 || die "Python 3 is not installed."
command -v "${TOOLCHAIN}gcc" >/dev/null 2>&1 || die "${TOOLCHAIN}gcc not found!"

echo "SOC:           $SOC"
echo "BOARD:         $BOARD"
echo "Toolchain:     $TOOLCHAIN"
echo "STAGING_DIR:   $STAGING_DIR"
echo "Defconfig:     $UBOOT_CFG"

[ -f "$UBOOT_DIR/configs/$UBOOT_CFG" ] || die "Defconfig not found: $UBOOT_DIR/configs/$UBOOT_CFG"

# ---------------------------------------------------------------------------
# Parallel jobs
# ---------------------------------------------------------------------------
if [ -z "$JOBS" ]; then
	if command -v nproc >/dev/null 2>&1; then
		JOBS=$(nproc)
	else
		JOBS=1
	fi
fi

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
echo "======================================================================"
echo "Building U-Boot..."
echo "======================================================================"

rm -f "$UBOOT_DIR/u-boot.bin" "$UBOOT_DIR/u-boot-with-spl.bin"
cp -f "$UBOOT_DIR/configs/$UBOOT_CFG" "$UBOOT_DIR/.config"
make -C "$UBOOT_DIR" olddefconfig
make -C "$UBOOT_DIR" clean
make -C "$UBOOT_DIR" CROSS_COMPILE="${TOOLCHAIN}" STAGING_DIR="${STAGING_DIR}" -j "$JOBS" all

# Determine output image: respect CONFIG_BUILD_TARGET (e.g. u-boot-with-spl.bin for SPL builds)
UBOOT_BIN=$(get_config "BUILD_TARGET")
UBOOT_BIN=$(echo "$UBOOT_BIN" | tr -d '"')
[ -n "$UBOOT_BIN" ] || UBOOT_BIN="u-boot.bin"

[ -f "$UBOOT_DIR/$UBOOT_BIN" ] || die "U-Boot build failed! $UBOOT_BIN not generated."
echo "U-Boot build done! (image: $UBOOT_BIN)"

# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------
echo "======================================================================"
echo "Copying output files..."
echo "======================================================================"

mkdir -p "$OUTPUT_DIR"

MD5SUM=$(md5sum "$UBOOT_DIR/$UBOOT_BIN" | awk '{print $1}')
echo "$UBOOT_BIN md5: $MD5SUM"

UBOOTNAME="${SOC_ID}-u-boot-${BOARD}.bin"
cp -f "$UBOOT_DIR/$UBOOT_BIN" "$OUTPUT_DIR/$UBOOTNAME"

echo "${SOC_ID}-u-boot-${BOARD} build done"
echo "Output: $OUTPUT_DIR/$UBOOTNAME"
