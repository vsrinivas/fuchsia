#!/bin/bash
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FUCHSIA_DIR=""
FUCHSIA_BUILD_DIR=""
DEVICE_NAME=""
LOCAL_HOSTNAME=""
DEVICE_HOSTNAME=""

while (($#)); do
  case "$1" in
    --fuchsia-dir)
      FUCHSIA_DIR="$2"
      shift
      ;;
    --fuchsia-build-dir)
      FUCHSIA_BUILD_DIR="$2"
      shift
      ;;
    --local-hostname)
      LOCAL_HOSTNAME="$2"
      shift
      ;;
    -d|--device)
      DEVICE_NAME="$2"
      shift
      ;;
    --device-hostname)
      DEVICE_HOSTNAME="$2"
      shift
      ;;
    *)
      echo "Unrecognized option: $1"
      exit 1
  esac
  shift
done

if [[ -z "${FUCHSIA_DIR}" ]]; then
  echo >&2 "--fuchsia-dir must be specified"
  exit 1
fi

if [[ -z "${FUCHSIA_BUILD_DIR}" ]]; then
  echo >&2 "--fuchsia-build-dir must be specified"
  exit 1
fi

export FUCHSIA_DIR
export FUCHSIA_BUILD_DIR

if [[ -n "${DEVICE_NAME}" && -n "${DEVICE_HOSTNAME}" ]]; then
  echo >&2 "--device and --hostname are incompatible"
  exit 1
fi

if [[ -z "${LOCAL_HOSTNAME}" ]]; then
  LOCAL_HOSTNAME=$($TEST_DIR/lib/netaddr.sh --local "${DEVICE_NAME}")
  if [[ $? -ne 0 || -z "$LOCAL_HOSTNAME" ]]; then
    echo >&2 "Unable to determine localhost's IP"
    exit 1
  fi
fi

if [[ -z "${DEVICE_HOSTNAME}" ]]; then
  DEVICE_HOSTNAME=$($TEST_DIR/lib/netaddr.sh --nowait --timeout=1000 --fuchsia "${DEVICE_NAME}")
  if [[ $? -ne 0 || -z "$DEVICE_HOSTNAME" ]]; then
    echo >&2 "Unable to determine target device's IP.  Is the target up?"
    exit 1
  fi
fi

export LOCAL_HOSTNAME
export DEVICE_HOSTNAME

cleanup() {
  # kill child processes
  local child_pids=$(jobs -p)
  if [[ -n "${child_pids}" ]]; then
    # Note: child_pids must be expanded to args here.
    kill ${child_pids} 2> /dev/null
    wait 2> /dev/null
  fi
}
trap cleanup EXIT

log() {
  printf "$(date '+%Y-%m-%d %H:%M:%S') [ota-test] %s\n" "$@"
}

run() {
  (
    set -x
    "$@"
  )
}

die() {
  if [[ -n "$1" ]]; then
    log "TEST FAILED - $@"
  else
    log "TEST FAILED"
  fi
  exit 1
}

if [[ "$(uname -s)" = "Darwin" ]]; then
  ping_device() {
    # Darwin's "ping" doesn't recognize IPv6 addresses and doesn't accept a
    # timeout parameter, which appears to default to 10 seconds.
    ping6 -c 1 "$1" >/dev/null
  }
else
  ping_device() {
    ping -c 1 -W 1 "$1" >/dev/null
  }
fi

wait_for_device_to_ping() {
  while true; do
    if ping_device "$DEVICE_HOSTNAME"; then
      break
    fi

    sleep 1
  done
}

wait_for_device_to_not_ping() {
  while true; do
    if ping_device "${DEVICE_HOSTNAME}"; then
      sleep 1
    else
      break
    fi
  done
}

start_pm_serve() {
  log "starting pm serve"

  ${FUCHSIA_BUILD_DIR}/host_x64/pm serve -d "${FUCHSIA_BUILD_DIR}/amber-files/repository" &
  local pm_srv_pid=$!

  # Allow a little slack for pm-srv to startup, that way the first kill -0 will catch a dead pm-srv.
  sleep 1
  if ! kill -0 "${pm_srv_pid}" 2> /dev/null; then
    log "Amber Server died, exiting"
    wait
    exit $?
  fi
}

register_amber_source() {
  log "registering devhost as update source"

  if $TEST_DIR/lib/add-update-source.sh --name devhost --type devhost --local-hostname "${LOCAL_HOSTNAME}" --device-hostname "${DEVICE_HOSTNAME}"; then
    log "Ready to push packages!"
  else
    log "Device lost while configuring update source"
    exit 1
  fi

  sleep 1

  if ping_device "$DEVICE_HOSTNAME"; then
    sleep 1
  else
    log "Device lost"
    exit 1
  fi
}

wait_for_path() {
  path="$1"
  if [[ -z "${path}" ]]; then
    log "wait_for_path() missing path argument"
    exit 1
  fi

  while true; do
    log "waiting for ${path} to mount"
    "${TEST_DIR}/lib/ssh.sh" "${DEVICE_HOSTNAME}" "ls ${path}"
    if [[ $? -eq 0 ]]; then
      break
    fi
    sleep 1
  done
}

reboot_file_path="/tmp/ota-test-waiting-for-reboot"

touch_reboot_file() {
  log "touching ${reboot_file_path}"
  "${TEST_DIR}/lib/ssh.sh" "${DEVICE_HOSTNAME}" "touch ${reboot_file_path}"
  if [[ $? -ne 0 ]]; then
    log "failed to touch ${reboot_file_path}"
    exit 1
  fi
}

wait_for_fuchsia_to_reboot() {
  log "making sure device rebooted by watching if ${reboot_file_path} still exists"
  while true; do
    "${TEST_DIR}/lib/ssh.sh" "${DEVICE_HOSTNAME}" "ls $reboot_file_path" 2>&1 | grep -q "No such file or directory"
    if [[ $? -eq 0 ]]; then
      break
    fi
    sleep 1
  done

  wait_for_path "/system"
  wait_for_path "/pkgfs"
}

check_file_absent() {
  local local_path=$1
  local remote_path=$2

  local expected=$(cat "${local_path}")
  if [[ $? -ne 0 ]]; then
    log "failed to read $local_path"
    exit 1
  fi

  log "checking that $remote_path does not exist or have the expected value"
  local actual=$("${TEST_DIR}/lib/scp.sh" "[${DEVICE_HOSTNAME}]:$remote_path" "/dev/stdout")
  if [[ $? -eq 0 && "${expected}" == "${actual}" ]]; then
    log "file exists and has the value:\n${actual}"
    return 1
  fi
}

check_file_exists() {
  local local_path=$1
  local remote_path=$2

  local expected=$(cat "${local_path}")
  if [[ $? -ne 0 ]]; then
    log "failed to read $local_path"
    exit 1
  fi

  log "checking if $remote_path has expected value"
  local actual=$("${TEST_DIR}/lib/scp.sh" "[${DEVICE_HOSTNAME}]:$remote_path" "/dev/stdout")
  if [[ $? -ne 0 ]]; then
    log "failed to read $remote_path"
    return 1
  fi

  if [[ "${expected}" == "${actual}" ]]; then
    return 0
  else
    log "Expected: '${expected}' but recieved: '${actual}'"
    return 1
  fi
}

main() {
  local LOCAL_DEVMGR="${FUCHSIA_BUILD_DIR}/obj/build/images/devmgr_config.txt"
  local REMOTE_DEVMGR="/boot/config/devmgr"

  local EXPECTED_DATA="${FUCHSIA_BUILD_DIR}/obj/garnet/go/src/amber/tests/ota/test-data"
  local REMOTE_PACKAGE_DATA="/pkgfs/packages/ota-test-package/0/data/test-data"
  local REMOTE_SYSTEM_DATA="/system/data/ota-test/test-data"

  log "waiting for fuchsia to be ready"
  wait_for_device_to_ping
  log "device up"

  log "verifying fuchsia is running an old version"
  check_file_absent "${LOCAL_DEVMGR}" "${REMOTE_DEVMGR}" || die
  echo

  log "verifying fuchsia does not have ${REMOTE_PACKAGE_DATA}"
  check_file_absent "${EXPECTED_DATA}" "${REMOTE_SYSTEM_DATA}" || die
  echo

  log "verifying fuchsia does not have ${REMOTE_SYSTEM_DATA}"
  check_file_absent "${EXPECTED_DATA}" "${REMOTE_PACKAGE_DATA}" || die
  echo

  start_pm_serve
  echo

  register_amber_source
  echo

  # We touch a file in /tmp before triggering an OTA. If the OTA is successful,
  # the device should reboot, which will wipe away /tmp.
  touch_reboot_file
  echo

  log "triggering OTA"
  run "${TEST_DIR}/lib/ssh.sh" "${DEVICE_HOSTNAME}" "amber_ctl system_update" || die "triggering OTA failed"
  echo

  log "waiting for fuchsia to reboot"
  wait_for_fuchsia_to_reboot || die

  log "verifying fuchsia is running the new version"
  check_file_exists "${LOCAL_DEVMGR}" "${REMOTE_DEVMGR}" || die
  echo

  log "verifying fuchsia has our $REMOTE_SYSTEM_DATA"
  check_file_exists "${EXPECTED_DATA}" "${REMOTE_SYSTEM_DATA}" || die
  echo

  log "verifying fuchsia has our $REMOTE_PACKAGE_DATA"
  check_file_exists "${EXPECTED_DATA}" "${REMOTE_PACKAGE_DATA}" || die

  echo "ALL TESTS PASSED"
}

main
