#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

fx-config-read
source "${FUCHSIA_DIR}/buildtools/vars.sh"

get-fuchsia-device-addr-or-die() {
  got=$(get-fuchsia-device-addr)
  if [[ -z "${got}" ]]; then
    fx-error "Could not find fuchsia device" "$(get-device-name)"
    exit 1
  fi
  echo ${got}
}

# Starts a debug agent on the target.  Requires $port to be set to the port you
# want the debug agent to start, and $agent_out to be set to where you want the
# agent's log output.
launch_debug_agent() {
  debug_agent_pkg="fuchsia-pkg://fuchsia.com/debug_agent#meta/debug_agent.cmx"

  fx_ssh_pid=""
  trap 'kill ${fx_ssh_pid} > /dev/null 2>&1' EXIT

# Get the defaulted device address.
  target=$(get-fuchsia-device-addr-or-die)

# Leave the SSH connection open. Will be closed on script end.
# We branch out on whether the user used the verbose-agent flag. If so, we
# redirect the debug agent output to /dev/null.
  echo -e "Attempting to start the Debug Agent."
  fx-command-run "ssh" "${target}" "run ${debug_agent_pkg} --port=${port} ${unwind_flag}" > "${agent_out}" 2>&1 &
  fx_ssh_pid="$!"

# We wait until the debug agent is listening on the given port. We use NC to
# attemp a tcp connection. This will actually go all the way with the handshake,
# so the debug agent will think initially that NC is a client. But then it will
# close the connection and receive the actual client's connection and work fine.
  try_count=0
  max_tries=10
  echo "Waiting for the Debug Agent to start."
  while true; do
    # Use NC to test if the port is open and the debug agent is listening.
    if nc -w5 -6 -z "${target}" "${port}"; then
      break
    fi

    # Otherwise, we count and check if we need to exit.
    try_count=$(expr "${try_count}" + 1)
    if [[ "${try_count}" -gt "${max_tries}" ]]; then
      fx-error "Timed out trying to find the Debug Agent. Exiting."
      exit 1
    fi
    sleep 1
  done
}
