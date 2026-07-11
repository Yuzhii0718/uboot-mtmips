#!/bin/sh
# ============================================================================
# qcamips.sh - Build U-Boot for QCA MIPS32 platform
#              (QCA953X / QCA955X / QCA956X / AP135 / AP143 / AP152)
#
# Usage:
#   SOC=<qca953x|qca955x|qca956x|ap135|ap143|ap152> ./qcamips.sh
#
# Examples:
#   SOC=qca953x ./qcamips.sh
#   SOC=qca955x ./qcamips.sh
#   SOC=qca956x ./qcamips.sh
#   SOC=ap135  ./qcamips.sh
#   SOC=ap143  ./qcamips.sh
#   SOC=ap152  ./qcamips.sh
#
# Environment variables:
#   SOC           Target SoC (required: qca953x, qca955x, qca956x, ap135, ap143, ap152)
#   TOOLCHAIN     Cross-compiler prefix (auto-detected if empty)
#   JOBS          Parallel make jobs (default: nproc)
#   STAGING_DIR   Staging directory (passed to make)
#
# Output:
#   output_qcamips/<soc>-u-boot.bin
#
# Toolchain:
#   https://downloads.openwrt.org/releases/25.12.5/targets/ath79/generic/
#   openwrt-toolchain-25.12.5-ath79-generic_gcc-14.3.0_musl.Linux-x86_64.tar.zst
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
Usage: SOC=<qca953x|qca955x|qca956x|ap135|ap143|ap152> [OPTIONS] ./qcamips.sh

Build U-Boot for QCA MIPS32 (QCA953X / QCA955X / QCA956X / AP135 / AP143 / AP152) platform.

Required:
  SOC=<qca953x|qca955x|qca956x|ap135|ap143|ap152>   Target SoC

Options:
  TOOLCHAIN=...   Cross-compiler prefix (auto-detected from ../openwrt*/toolchain-mips*/)
  JOBS=<n>        Parallel make jobs (default: nproc)
  STAGING_DIR=... Staging directory (auto-detected from TOOLCHAIN)

Examples:
  SOC=qca953x ./qcamips.sh
  SOC=qca955x ./qcamips.sh
  SOC=qca956x ./qcamips.sh
  SOC=ap135  ./qcamips.sh
  SOC=ap143  ./qcamips.sh
  SOC=ap152  ./qcamips.sh
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
	echo "Usage: SOC=<qca953x|qca955x|qca956x|ap135|ap143|ap152> $0"
	echo "Try '$0 --help' for more information."
	exit 1
fi

case "$SOC" in
	qca953x|qca955x|qca956x|tp934x|ap135|ap143|ap152|cus249)
		SOC_DEFCONFIG="$SOC"
		;;
	*)
		echo "Error: Unsupported SOC='$SOC'. Valid values: qca953x, qca955x, qca956x, tp934x, ap135, ap143, ap152, cus249"
		exit 1
		;;
esac

UBOOT_DIR=.
OUTPUT_DIR="output_qcamips"

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

# ---------------------------------------------------------------------------
# Toolchain: ath79 target, MIPS big-endian
# ---------------------------------------------------------------------------
TOOLCHAIN_SUBPATH="ath79/generic"
TOOLCHAIN_PATTERN="openwrt*toolchain*ath79*"
TOOLCHAIN_URL="${TOOLCHAIN_URL:-https://downloads.openwrt.org/releases/25.12.5/targets/ath79/generic/openwrt-toolchain-25.12.5-ath79-generic_gcc-14.3.0_musl.Linux-x86_64.tar.zst}"

# ---------------------------------------------------------------------------
# Auto-detect or download toolchain (look in parent directory, not U-Boot tree)
# ---------------------------------------------------------------------------
PARENT_DIR="$(cd "$UBOOT_DIR/.."; pwd)"

find_toolchain() {
	TOOLCHAIN_BIN=""
	for dir in $PARENT_DIR/$TOOLCHAIN_PATTERN/toolchain-mips*/bin; do
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
				die "Toolchain required. Set TOOLCHAIN=... or place ${TOOLCHAIN_PATTERN}/toolchain-mips*/ in $PARENT_DIR."
				;;
		esac
	fi
	TOOLCHAIN="${TOOLCHAIN_BIN}/mips-openwrt-linux-"
fi

if [ -z "$STAGING_DIR" ]; then
	STAGING_DIR="${TOOLCHAIN_BIN%/bin}"
fi

# ---------------------------------------------------------------------------
# Build config
# ---------------------------------------------------------------------------
UBOOT_CFG="${SOC_DEFCONFIG}_defconfig"

# ---------------------------------------------------------------------------
# Environment checks
# ---------------------------------------------------------------------------
echo "======================================================================"
echo "QCA MIPS32 U-Boot Build (SOC=${SOC})"
echo "======================================================================"

command -v python3 >/dev/null 2>&1 || die "Python 3 is not installed."
command -v "${TOOLCHAIN}gcc" >/dev/null 2>&1 || die "${TOOLCHAIN}gcc not found!"

echo "SOC:           $SOC"
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

rm -f "$UBOOT_DIR/u-boot.bin"
cp -f "$UBOOT_DIR/configs/$UBOOT_CFG" "$UBOOT_DIR/.config"
PATH="${TOOLCHAIN_BIN}:${PATH}" make -C "$UBOOT_DIR" olddefconfig
PATH="${TOOLCHAIN_BIN}:${PATH}" make -C "$UBOOT_DIR" clean
PATH="${TOOLCHAIN_BIN}:${PATH}" make -C "$UBOOT_DIR" CROSS_COMPILE="${TOOLCHAIN}" STAGING_DIR="${STAGING_DIR}" -j "$JOBS" all

# Determine output image: respect CONFIG_BUILD_TARGET
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

UBOOTNAME="${SOC}-u-boot.bin"
cp -f "$UBOOT_DIR/$UBOOT_BIN" "$OUTPUT_DIR/$UBOOTNAME"

echo "${SOC} build done"
echo "Output: $OUTPUT_DIR/$UBOOTNAME"
