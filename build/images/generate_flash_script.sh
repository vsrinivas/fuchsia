#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# The generated script behavior depends on whether the FVM is flashable,
# as indicated by the --fvm partition arg.
#
# If --fvm is present, the resulting script will flash the full OS:
#   * Zircon image to A/B slots
#   * recovery image to R slot
#   * FVM image to the fvm partition
#
# Otherwise, the script will instead flash the recovery image to all slots,
# since this will normally be a Zedboot image which does not use an FVM and
# allows the user to then pave the full OS.
#
# The script supports a "--ssh-key" arg which specifies the ssh key file to
# provision to the device.
#
# The script also supports a "--recovery" arg which always flashes recovery
# images whether the FVM is available or not.

ZIRCON_IMAGE=
RECOVERY_IMAGE=
FVM_IMAGE=
OUTPUT=
ZIRCON_A_PARTITION=
ZIRCON_B_PARTITION=
ZIRCON_R_PARTITION=
VBMETA_A_PARTITION=
VBMETA_B_PARTITION=
VBMETA_R_PARTITION=
FVM_PARTITION=
ACTIVE_PARTITION=
PRODUCT=
PRE_ERASE_FLASH=
FASTBOOT_PATH=
FIRMWARE=()

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
    --recovery-image=*)
    RECOVERY_IMAGE="${i#*=}"
    shift
    ;;
    --fvm-image=*)
    FVM_IMAGE="${i#*=}"
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
    --fvm=*)
    FVM_PARTITION="${i#*=}"
    shift
    ;;
    --active=*)
    ACTIVE_PARTITION="${i#*=}"
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
    --firmware=*)
    FIRMWARE+=("${i#*=}")
    shift
    ;;
esac
done

# If we can't flash the FVM, put the recovery image in all slots.
if [[ -z "${FVM_PARTITION}" ]]; then
  ZIRCON_IMAGE="${RECOVERY_IMAGE}"
fi

ZIRCON_VBMETA="${ZIRCON_IMAGE%%.*}.vbmeta"
RECOVERY_VBMETA="${RECOVERY_IMAGE%%.*}.vbmeta"

# Support a single --recovery flag which flashes recovery to all slots even
# if the full image is available.
cat > "${OUTPUT}" << EOF
#!/bin/sh
DIR="\$(dirname "\$0")"
set -e

ZIRCON_IMAGE=${ZIRCON_IMAGE}
ZIRCON_VBMETA=${ZIRCON_VBMETA}
RECOVERY_IMAGE=${RECOVERY_IMAGE}
RECOVERY_VBMETA=${RECOVERY_VBMETA}
RECOVERY=
SSH_KEY=

for i in "\$@"
do
case \$i in
    --recovery)
    RECOVERY=true
    ZIRCON_IMAGE=${RECOVERY_IMAGE}
    ZIRCON_VBMETA=${RECOVERY_VBMETA}
    shift
    ;;
    --ssh-key=*)
    SSH_KEY="\${i#*=}"
    shift
    ;;
    *)
    break
    ;;
esac
done

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

for firmware in "${FIRMWARE[@]}"; do
  # Arg format is <partition>:<path>.
  fw_part="${firmware%%:*}"
  fw_path="${firmware#*:}"
  erase_raw_flash "${fw_part}"
  echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" flash "${fw_part}" \"\${DIR}/${fw_path}\" "${extra_args[@]}" >> "${OUTPUT}"
done

# Reboot into bootloader so that the new firmware image takes effect.
if [[ ! -z "${FIRMWARE[@]}" ]]; then
  echo "${FASTBOOT_PATH} reboot bootloader" >> "${OUTPUT}"
  # Wait for 1 seconds so that the reboot process can be recognized by the script.
  echo "sleep 1" >> "${OUTPUT}"
fi

if [[ ! -z "${ZIRCON_A_PARTITION}" ]]; then
  erase_raw_flash ${ZIRCON_A_PARTITION}
  echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" flash "${ZIRCON_A_PARTITION}" \"\${DIR}/\${ZIRCON_IMAGE}\" "${extra_args[@]}" >> "${OUTPUT}"
fi
if [[ ! -z "${ZIRCON_B_PARTITION}" ]]; then
  erase_raw_flash ${ZIRCON_B_PARTITION}
  echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" flash "${ZIRCON_B_PARTITION}" \"\${DIR}/\${ZIRCON_IMAGE}\" "${extra_args[@]}" >> "${OUTPUT}"
fi
if [[ ! -z "${ZIRCON_R_PARTITION}" ]]; then
  erase_raw_flash ${ZIRCON_R_PARTITION}
  echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" flash "${ZIRCON_R_PARTITION}" \"\${DIR}/\${RECOVERY_IMAGE}\" "${extra_args[@]}" >> "${OUTPUT}"
fi
if [[ ! -z "${VBMETA_A_PARTITION}" ]]; then
  erase_raw_flash ${VBMETA_A_PARTITION}
  echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" flash "${VBMETA_A_PARTITION}" \"\${DIR}/\${ZIRCON_VBMETA}\" "${extra_args[@]}" >> "${OUTPUT}"
fi
if [[ ! -z "${VBMETA_B_PARTITION}" ]]; then
  erase_raw_flash ${VBMETA_B_PARTITION}
  echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" flash "${VBMETA_B_PARTITION}" \"\${DIR}/\${ZIRCON_VBMETA}\" "${extra_args[@]}" >> "${OUTPUT}"
fi
if [[ ! -z "${VBMETA_R_PARTITION}" ]]; then
  erase_raw_flash ${VBMETA_R_PARTITION}
  echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" flash "${VBMETA_R_PARTITION}" \"\${DIR}/\${RECOVERY_VBMETA}\" "${extra_args[@]}" >> "${OUTPUT}"
fi
if [[ ! -z "${ACTIVE_PARTITION}" ]]; then
  echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" set_active "${ACTIVE_PARTITION}" >> "${OUTPUT}"
fi
if [[ ! -z "${FVM_PARTITION}" ]]; then
  # The FVM partition takes a significant amount of time to flash (40s+), so
  # it's worth skipping if we're only flashing recovery and don't need it.
  echo "if [[ -z \"\${RECOVERY}\" ]]; then" >> "${OUTPUT}"
  erase_raw_flash ${FVM_PARTITION}
  echo "  ${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" flash "${FVM_PARTITION}" \"\${DIR}/${FVM_IMAGE}\" "${extra_args[@]}" >> "${OUTPUT}"
  echo "fi" >> "${OUTPUT}"

  # Provision SSH key from fastboot if --ssh-key was given.
  echo "if [[ ! -z \"\${SSH_KEY}\" ]]; then" >> "${OUTPUT}"
  echo "  ${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" "stage \"\${SSH_KEY}\"" >> "${OUTPUT}"
  echo "  ${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" "oem add-staged-bootloader-file ssh.authorized_keys" >> "${OUTPUT}"
  echo "fi" >> "${OUTPUT}"

  echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" continue >> "${OUTPUT}"
else
  # TODO(60172): switch back to `fastboot continue` everywhere once all boards
  # support it. For now we can only assume boards that support FVM + keys know
  # how to continue.
  echo "${FASTBOOT_PATH}" "\${FASTBOOT_ARGS}" reboot >> "${OUTPUT}"
fi

chmod +x "${OUTPUT}"
