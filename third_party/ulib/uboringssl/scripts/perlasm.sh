#! /bin/sh

BASEDIR=$(dirname $0)
ASM_DIR=${BASEDIR}/../asm
SRC_DIR=${BASEDIR}/../crypto

perl ${SRC_DIR}/chacha/asm/chacha-armv8.pl linux64 ${ASM_DIR}/chacha-arm64.S
perl ${SRC_DIR}/chacha/asm/chacha-x86_64.pl ${ASM_DIR}/chacha-x86-64.S

perl ${SRC_DIR}/fipsmodule/sha/asm/sha512-armv8.pl linux64 ${ASM_DIR}/sha256-arm64.S
perl ${SRC_DIR}/fipsmodule/sha/asm/sha512-x86_64.pl ${ASM_DIR}/sha256-x86-64.S

perl ${SRC_DIR}/fipsmodule/sha/asm/sha512-armv8.pl linux64 ${ASM_DIR}/sha512-arm64.S
perl ${SRC_DIR}/fipsmodule/sha/asm/sha512-x86_64.pl ${ASM_DIR}/sha512-x86-64.S
