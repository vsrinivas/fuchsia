# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This library is used by:
# * debug
# * fidlcat

# Checks to see if a debug agent is listening by knocking on the agent
# port with nc(1).
# 1: agent port
# 2: number of tries
check_for_agent() {
  local port="$1"
  local num_tries="$2"

  while ((num_tries--)); do
    if nc -w5 -z "$(get-fuchsia-device-addr)" "${port}"; then
      return 0
    fi
    sleep 1
  done
  return 1
}

# Starts a debug agent on the target if it's not there.
# 1: agent port
# 2: agent unwind flag
# 3: agent output path
# The agent is started in a subprocess that must be waited upon.
launch_debug_agent() {
  local port="$1"
  local unwind_flag="$2"
  local agent_out="$3"
  local debug_agent_url="fuchsia-pkg://fuchsia.com/debug_agent#meta/debug_agent.cmx"

  # See if the debug agent is already there.
  echo "Checking for debug agent on $(get-device-addr-resource):${port}."
  if fx-command-run "shell" "ps" | grep "debug_agent.cmx" > /dev/null; then
    echo "Found agent already running."
    return 0
  fi

  # Leave the SSH connection open. Will be closed on script end.
  # We branch out on whether the user used the verbose-agent flag. If so, we
  # redirect the debug agent output to /dev/null.
  echo -e "Debug agent not found. Starting one."
  fx-command-run "run" "${debug_agent_url}" "--port=${port}" "${unwind_flag}" > "${agent_out}" 2>&1 &

  # Bug: 49094, with serve-remote this can not detect a running
  # agent the way that check_for_agent attempts to, we sleep a very
  # short time to give the agent a chance to actually start. Ideally
  # zxdb(1) should handle this instead.
  sleep 0.5

  # Wait for the debug agent to start.
  if check_for_agent "${port}" 10; then
    return 0
  else
    fx-error "Timed out trying to find the Debug Agent."
    return 1
  fi
}
