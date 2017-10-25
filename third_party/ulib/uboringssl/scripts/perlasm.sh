#! /bin/sh

BASEDIR=$(dirname $0)
ASM_DIR=${BASEDIR}/../asm
SRC_DIR=${BASEDIR}/../crypto

# ARM/aarch64

## Ciphers
perl ${SRC_DIR}/chacha/asm/chacha-armv8.pl linux64 ${ASM_DIR}/chacha-arm64.S
perl ${SRC_DIR}/fipsmodule/aes/asm/aesv8-armx.pl linux64 ${ASM_DIR}/aes-arm64.S
perl ${SRC_DIR}/fipsmodule/modes/asm/ghashv8-armx.pl linux64 ${ASM_DIR}/ghash-arm64.S

## Digests
perl ${SRC_DIR}/fipsmodule/sha/asm/sha1-armv8.pl linux64 ${ASM_DIR}/sha1-arm64.S
perl ${SRC_DIR}/fipsmodule/sha/asm/sha512-armv8.pl linux64 ${ASM_DIR}/sha256-arm64.S
perl ${SRC_DIR}/fipsmodule/sha/asm/sha512-armv8.pl linux64 ${ASM_DIR}/sha512-arm64.S

# Intel/x86-64

## Ciphers
perl ${SRC_DIR}/chacha/asm/chacha-x86_64.pl '' ${ASM_DIR}/chacha-x86-64.S
perl ${SRC_DIR}/cipher_extra/asm/aes128gcmsiv-x86_64.pl '' ${ASM_DIR}/aes128gcmsiv-x86-64.S
perl ${SRC_DIR}/fipsmodule/aes/asm/aes-x86_64.pl '' ${ASM_DIR}/aes-x86-64.S
perl ${SRC_DIR}/fipsmodule/aes/asm/aesni-x86_64.pl '' ${ASM_DIR}/aesni-x86-64.S
perl ${SRC_DIR}/fipsmodule/aes/asm/bsaes-x86_64.pl '' ${ASM_DIR}/bsaes-x86-64.S
perl ${SRC_DIR}/fipsmodule/aes/asm/vpaes-x86_64.pl '' ${ASM_DIR}/vpaes-x86-64.S
perl ${SRC_DIR}/fipsmodule/modes/asm/aesni-gcm-x86_64.pl linux64 ${ASM_DIR}/aesnigcm-x86-64.S
perl ${SRC_DIR}/fipsmodule/modes/asm/ghash-x86_64.pl linux64 ${ASM_DIR}/ghash-x86-64.S

## Digests
perl ${SRC_DIR}/fipsmodule/md5/asm/md5-x86_64.pl '' ${ASM_DIR}/md5-x86-64.S
perl ${SRC_DIR}/fipsmodule/sha/asm/sha1-x86_64.pl '' ${ASM_DIR}/sha1-x86-64.S
perl ${SRC_DIR}/fipsmodule/sha/asm/sha512-x86_64.pl '' ${ASM_DIR}/sha256-x86-64.S
perl ${SRC_DIR}/fipsmodule/sha/asm/sha512-x86_64.pl '' ${ASM_DIR}/sha512-x86-64.S

## RNGs
perl ${SRC_DIR}/fipsmodule/rand/asm/rdrand-x86_64.pl '' ${ASM_DIR}/rdrand-x86-64.S
