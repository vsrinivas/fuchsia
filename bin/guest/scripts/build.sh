#!/usr/bin/env bash

set -e

usage() {
    echo "usage: ${0} [options] {arm64, x86}"
    echo
    echo "  -A            Use ASAN in GN"
    echo "  -g            Use Goma"
    echo "  -p [package]  Set package, defaults to 'garnet/packages/guest'"
    echo
    exit 1
}

while getopts "Agp:" FLAG; do
    case "${FLAG}" in
    A)  GN_ASAN="--variant=asan";;
    g)  $HOME/goma/goma_ctl.py ensure_start;
        GN_GOMA="--goma";
        NINJA_GOMA="-j1024";;
    p)  PACKAGE="${OPTARG}";;
    *)  usage;;
    esac
done
shift $((OPTIND - 1))

case "${1}" in
arm64)
    PLATFORM="zircon-hikey960-arm64";
    ARCH="aarch64";;
x86)
    PLATFORM="zircon-pc-x86-64";
    ARCH="x86-64";;
*)  usage;;
esac

GUEST_SCRIPTS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
FUCHSIA_DIR="${GUEST_SCRIPTS_DIR}/../../../.."
cd "${FUCHSIA_DIR}"

scripts/build-zircon.sh \
    -p $PLATFORM

build/gn/gen.py \
    --target_cpu=$ARCH \
    --platforms=$PLATFORM \
    --packages="${PACKAGE:-garnet/packages/guest}" \
    $GN_ASAN \
    $GN_GOMA

buildtools/ninja \
    -C out/debug-$ARCH \
    $NINJA_GOMA

garnet/bin/guest/scripts/mkbootfs.sh \
    -f out/debug-$ARCH/user.bootfs \
    out/build-zircon/build-$PLATFORM

case "${1}" in
arm64)
    zircon/scripts/flash-hikey \
        -u \
        -n \
        -b out/build-zircon/build-$PLATFORM \
        -d bootdata-with-guest.bin;;
x86)
    out/build-zircon/tools/bootserver \
        -1 \
        out/build-zircon/build-$PLATFORM/zircon.bin \
        bootdata-with-guest.bin;;
esac
