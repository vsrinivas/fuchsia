#! /bin/sh

BASEDIR=$(dirname $0)
ASM_DIR=${BASEDIR}/../asm
SRC_DIR=${BASEDIR}/../crypto

perl ${SRC_DIR}/chacha/asm/chacha-armv8.pl linux64 ${ASM_DIR}/chacha-arm64.S
perl ${SRC_DIR}/chacha/asm/chacha-x86_64.pl ${ASM_DIR}/chacha-x86-64.S
