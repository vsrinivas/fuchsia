#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ZIRCON_IMAGE=
OUTPUT=
ZIRCON_A_PARTITION=
ZIRCON_B_PARTITION=
ZIRCON_R_PARTITION=
VBMETA_A_PARTITION=
VBMETA_B_PARTITION=
VBMETA_R_PARTITION=
ACTIVE_PARTITION=
SIGNED_IMAGE=
PRODUCT=
PRE_ERASE_FLASH=
FASTBOOT_PATH=

erase_raw_flash ()
{
  local partition=$1
  if [[ ${PRE_ERASE_FLASH} = "true" ]]; then
    echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" erase "${partition}" >> "${OUTPUT}"
  fi
}

for i in "$@"
do
case $i in
    --image=*)
    ZIRCON_IMAGE="${i#*=}"
    shift
    ;;
    --output=*)
    OUTPUT="${i#*=}"
    shift
    ;;
    --zircon-a=*)
    ZIRCON_A_PARTITION="${i#*=}"
    shift
    ;;
    --zircon-b=*)
    ZIRCON_B_PARTITION="${i#*=}"
    shift
    ;;
    --zircon-r=*)
    ZIRCON_R_PARTITION="${i#*=}"
    shift
    ;;
    --vbmeta-a=*)
    VBMETA_A_PARTITION="${i#*=}"
    shift
    ;;
    --vbmeta-b=*)
    VBMETA_B_PARTITION="${i#*=}"
    shift
    ;;
    --vbmeta-r=*)
    VBMETA_R_PARTITION="${i#*=}"
    shift
    ;;
    --active=*)
    ACTIVE_PARTITION="${i#*=}"
    shift
    ;;
    --signed=*)
    SIGNED_IMAGE="${i#*=}"
    shift
    ;;
    --product=*)
    PRODUCT="${i#*=}"
    shift
    ;;
    --pre-erase-flash=*)
    PRE_ERASE_FLASH="${i#*=}"
    shift
    ;;
    --fastboot-path=*)
    FASTBOOT_PATH="\"\$DIR/${i#*=}\""
    shift
    ;;
esac
done

VBMETA_IMAGE="${ZIRCON_IMAGE%.*}.vbmeta"

if [[ "${SIGNED_IMAGE}" == "true" ]]; then
  ZIRCON_IMAGE="${ZIRCON_IMAGE}.signed"
fi

cat > "${OUTPUT}" << EOF
#!/bin/sh
DIR="\$(dirname "\$0")"
set -e
FASTBOOT_ARGS="\$@"
EOF

if [[ ! -z "${PRODUCT}" ]]; then
  cat >> "${OUTPUT}" << EOF
PRODUCT="${PRODUCT}"
actual=\$(${FASTBOOT_PATH} \${FASTBOOT_ARGS} getvar product 2>&1 | head -n1 | cut -d' ' -f2-)
if [[ "\${actual}" != "\${PRODUCT}" ]]; then
  echo >&2 "Expected device \${PRODUCT} but found \${actual}"
  exit 1
fi
EOF
fi

if [[ ! -z "${ZIRCON_A_PARTITION}" ]]; then
  erase_raw_flash ${ZIRCON_A_PARTITION}
  echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" flash "${ZIRCON_A_PARTITION}" \"\${DIR}/${ZIRCON_IMAGE}\" "${extra_args[@]}" >> "${OUTPUT}"
fi
if [[ ! -z "${ZIRCON_B_PARTITION}" ]]; then
  erase_raw_flash ${ZIRCON_B_PARTITION}
  echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" flash "${ZIRCON_B_PARTITION}" \"\${DIR}/${ZIRCON_IMAGE}\" "${extra_args[@]}" >> "${OUTPUT}"
fi
if [[ ! -z "${ZIRCON_R_PARTITION}" ]]; then
  erase_raw_flash ${ZIRCON_R_PARTITION}
  echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" flash "${ZIRCON_R_PARTITION}" \"\${DIR}/${ZIRCON_IMAGE}\" "${extra_args[@]}" >> "${OUTPUT}"
fi
if [[ ! -z "${VBMETA_A_PARTITION}" ]]; then
  erase_raw_flash ${VBMETA_A_PARTITION}
  echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" flash "${VBMETA_A_PARTITION}" \"\${DIR}/${VBMETA_IMAGE}\" "${extra_args[@]}" >> "${OUTPUT}"
fi
if [[ ! -z "${VBMETA_B_PARTITION}" ]]; then
  erase_raw_flash ${VBMETA_B_PARTITION}
  echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" flash "${VBMETA_B_PARTITION}" \"\${DIR}/${VBMETA_IMAGE}\" "${extra_args[@]}" >> "${OUTPUT}"
fi
if [[ ! -z "${VBMETA_R_PARTITION}" ]]; then
  erase_raw_flash ${VBMETA_R_PARTITION}
  echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" flash "${VBMETA_R_PARTITION}" \"\${DIR}/${VBMETA_IMAGE}\" "${extra_args[@]}" >> "${OUTPUT}"
fi
if [[ ! -z "${ACTIVE_PARTITION}" ]]; then
  echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" set_active "${ACTIVE_PARTITION}" >> "${OUTPUT}"
fi
echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" reboot >> "${OUTPUT}"

chmod +x "${OUTPUT}"
