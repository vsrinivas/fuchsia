#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
set -eu

SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"

# Fuchsia command common functions.
# shellcheck disable=SC1090
source "${SCRIPT_SRC_DIR}/fuchsia-common.sh" || exit $?
# shellcheck disable=SC1090
source "${SCRIPT_SRC_DIR}/devshell/lib/verify-default-keys.sh" || exit $?

usage() {
  cat << EOF
usage: fserve-remote.sh [--no-serve] [--device-name <device hostname>] HOSTNAME REMOTE-PATH
    Uses SSH port forwarding to connect to a remote server and forward package serving and other connections to a local device.

  --device-name <device hostname>
      Connects to a device by looking up the given device hostname.
  --image <image name>
      Name of prebuilt image packages to serve. Required unless --no-serve is specified.
  --bucket <bucket name>
      Name of GCS bucket containing the image archive.
  --no-serve
      Only tunnel, do not start a package server.
  --no-check-ssh-keys
      Do not verify that the default SSH credentials are the same before serving.
  -v
      Enable verbose SSH logging.
  -x
      Enable debug
  --ttl
      Time to keep the tunnel open. Defaults to "infinity". The format
      of the time is the same as the sleep command. This is primarily
      intended for testing. Tests should use "0" to return immediately.

  HOSTNAME
      The hostname of the workstation you want to serve from
  REMOTE-PATH
      The path to the Fuchsia GN SDK bin directory on  "HOSTNAME"
EOF
}

DEBUG_FLAG=""
TTL_TIME="infinity"
START_SERVE=1
CHECK_SSH_KEYS=1
VERBOSE=0
REMOTE_HOST=""
REMOTE_DIR=""
DEVICE_NAME="$(get-fuchsia-property device-name)"
BUCKET="$(get-fuchsia-property bucket)"
IMAGE="$(get-fuchsia-property image)"

while [[ $# -ne 0 ]]; do
  case "$1" in
  --help|-h)
      usage
      exit 0
      ;;
  --no-serve)
    START_SERVE=0
    ;;
  --no-check-ssh-keys)
    CHECK_SSH_KEYS=0
    ;;
  --device-name)
    shift
    DEVICE_NAME="${1}"
    ;;
  --bucket)
    shift
    BUCKET="${1}"
    ;;
  --image)
    shift
    IMAGE="${1}"
    ;;
  -v)
    VERBOSE=1
    ;;
  -x)
    DEBUG_FLAG="-x"
    set -x
    ;;
  --ttl)
    shift
    TTL_TIME="${1}"
    ;;
  -*)
    fx-error "Unknown flag: $1"
    usage
    exit 1
    ;;
  *)
    if [[ -z "${REMOTE_HOST}" ]]; then
      REMOTE_HOST="$1"
    elif [[ -z "${REMOTE_DIR}" ]]; then
      REMOTE_DIR="$1"
    else
      fx-error "unexpected argument: '$1'"
      usage
    fi
    ;;
  esac
  shift
done

if [[ -z "${REMOTE_HOST}" ]]; then
  fx-error "HOSTNAME must be specified"
  usage
  exit 1
fi

if [[ -z "${REMOTE_DIR}" ]]; then
  fx-error "REMOTE-DIR must be specified"
  usage
  exit 1
fi

if ((START_SERVE)); then
  if [[ -z "${IMAGE}" ]]; then
    fx-error "--image must be specified when serving packages."
    usage
    exit 1
  fi
fi

if [[ "${DEVICE_NAME}" == "" ]]; then
    DEVICE_NAME="$(get-device-name)"
fi

# Determine the local device name/address to use.
if ! DEVICE_IP=$(get-device-ip-by-name "${DEVICE_NAME}"); then
  fx-error "unable to discover device. Is the target up?"
  exit 1
fi

if [[ -z "${DEVICE_IP}" ]]; then
  fx-error "unable to discover device. Is the target up?"
  exit 1
fi

if [[ "${BUCKET}" == "" ]]; then
  fx-warn "--bucket not set. Using the default bucket."
fi

echo "Using remote ${REMOTE_HOST}:${REMOTE_DIR}"
echo "Using target device ${DEVICE_NAME}"

# Verify that keys match.
if [[ "${CHECK_SSH_KEYS}" == "1" ]]; then
  verify_default_keys "" "${REMOTE_HOST}" "" || exit $?
fi

# set the device name as the default to avoid confusion, and clear out the IP address
set-fuchsia-property "device-name" "${DEVICE_NAME}"
set-fuchsia-property "device-ip" ""


# Use a dedicated ControlPath so script can manage a connection seperately from the user's. We
# intentionally do not use %h/%p in the control path because there can only be one forwarding
# session at a time (due to the local forward of 8083).
ssh_base_args_gn=(
  "${REMOTE_HOST}"
  -S "${HOME}/.ssh/control-fuchsia-fx-remote"
  -o "ControlMaster=auto"
  -t
)

if ((VERBOSE)); then
  ssh_base_args_gn+=( -o "LogLevel=DEBUG2")
fi


ssh_exit() {
  if ssh "${ssh_base_args_gn[@]}" -O check > /dev/null 2>&1; then
    if  ! ssh "${ssh_base_args_gn[@]}" -O exit > /dev/null 2>&1 ; then
      echo "Error exiting session: $?"
    fi
  fi
}

# When we exit the script, close the background ssh connection.
trap_exit() {
  ssh_exit
  exit
}
trap trap_exit EXIT
trap trap_exit SIGINT


# First we need to check if we already have a control master for the
# host, if we do, we might already have the forwards and so we don't
# need to worry about tearing down:
if ssh "${ssh_base_args_gn[@]}" -O check > /dev/null 2>&1; then
  # If there is already control master then exit it. We can't be sure its to the right host and it
  # also could be stale.
  fx-warn "Cleaning up existing remote session"
  ssh_exit
fi

  # If we didn't have a control master, and the device already has 8022
  # bound, then there's a good chance there's a stale sshd instance
  # running from another device or another session that will block the
  # forward, so we'll check for that and speculatively attempt to clean
  # it up. Unfortunately this means authing twice, but it's likely the
  # best we can do for now.
  if ssh "${ssh_base_args_gn[@]}" 'ss -ln | grep :8022' > /dev/null  2>&1; then
    fx-warn "Found existing port forwarding, attempting to kill remote sshd sessions."
    if  pkill_result="$(ssh "${ssh_base_args_gn[@]}" "pkill -u \$USER sshd"  2>&1)"; then
      fx-error "Unexpected message from remote: ${pkill_result}"
    else
      echo "SSH session cleaned up."
    fi
  fi

ssh_tunnel_args=(
  -6 # We want ipv6 binds for the port forwards
  -L "\*:8083:localhost:8083" # requests to the package server address locally go to the workstation
  -R "8022:[${DEVICE_IP}]:22" # requests from the workstation to ssh to localhost:8022 will make it to the target
  -R "2345:[${DEVICE_IP}]:2345" # requests from the workstation to 2345 are forwarded to the target for zxdb, fidlcat.
  -R "8443:[${DEVICE_IP}]:8443" # port 8443 on workstation to target port 8443
  -R "9080:[${DEVICE_IP}]:80" # port 9080 on workstation to target port 80
  -o "ExitOnForwardFailure=yes"
)

# Start tunneling session in background. It's started seperately from the command invocations below
# to allow the script to be consistent on how it is exited for both serve and non-serve cases. It
# also allows script to explicitly close the control session (to better avoid stale sshd sessions).

ssh "${ssh_base_args_gn[@]}" "${ssh_tunnel_args[@]}" -nT sleep "${TTL_TIME}" &
# Attempt to assert that the backgrounded ssh is alive and kicking, emulating -f as best we can.
ssh_pid=$!

# If there's a 2fa prompt, we may need a "human time" number of tries, which is why this is high.
tries=30
until ssh "${ssh_base_args_gn[@]}" -q -O check; do
  if ! kill -0 ${ssh_pid}; then
    fx-error "SSH tunnel terminated prematurely"
    exit 1
  fi
  if ! ((tries--)); then
    fx-error "SSH tunnel appears not to have succeeded"
    kill -TERM "${ssh_pid}"
    exit 1
  fi
  sleep 1
done

# Set the configuration properties to match the remote device. Set the IP address,
# and clear the device name to avoid any confusion.
remote_cmds=(
  "cd \$HOME" "&&" # change directories to home, to avoid issues if the remote dir was deleted out from under us.
  "cd ${REMOTE_DIR}" "&&"
  "./bin/fconfig.sh set device-ip 127.0.0.1" "&&"
  "./bin/fconfig.sh default device-name"
  )

if [[ "${BUCKET}" != "" ]]; then
    remote_cmds+=("&&" "./bin/fconfig.sh set bucket ${BUCKET}")
fi

if [[ "${IMAGE}" != "" ]]; then
    remote_cmds+=("&&" "./bin/fconfig.sh set image ${IMAGE}")
fi

# Run fconfig.sh list to print out the settings, this will help diagnosing any
# problems.
remote_cmds+=("&&" "./bin/fconfig.sh list")
if [[ "${DEBUG_FLAG}" != "" ]]; then
  remote_cmds+=("&&" "echo Desktop env is \$(env)")
fi
# The variables here should be expanded locally, disabling the shellcheck lint
# message about ssh and variables.
# shellcheck disable=SC2029
ssh "${ssh_base_args_gn[@]}" "${remote_cmds[@]}"

if ((START_SERVE)); then
# If the user requested serving, then we'll check to see if there's a
# server already running and kill it, this prevents most cases where
# signal propagation seems to sometimes not make it to "pm".
  if ssh "${ssh_base_args_gn[@]}" 'ss -ln | grep :8083' > /dev/null; then
    fx-warn "Cleaning up \`pm\` running on remote desktop"
    if ssh "${ssh_base_args_gn[@]}" "pkill -u \$USER pm"; then
      echo "Success"
    fi
  fi

  # Starts a package server
  args=(cd "\$HOME" "&&" cd "${REMOTE_DIR}" "&&" ./bin/fserve.sh)
  if [[ "${DEBUG_FLAG}" != "" ]]; then
    args+=("${DEBUG_FLAG}")
  fi
  # fserve.sh runs in the background, keep the tunnel open by running sleep.
  args+=("&&" "sleep ${TTL_TIME}")

  # shellcheck disable=SC2029
  ssh "${ssh_base_args_gn[@]}" "${args[@]}"
fi

echo "Press Ctrl-C to stop tunneling."

# Wait for user Ctrl-C. Then script exit will trigger trap_exit to close the ssh connection.
# a TTL_TIME of 0 is used in unit tests.
if [[ "$TTL_TIME" != "0" ]]; then
  read -r -d '' _ </dev/tty
fi
