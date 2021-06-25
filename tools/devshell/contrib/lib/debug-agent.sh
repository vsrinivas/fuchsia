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
#
# The only reliable way to check that a debug agent is listening on a given port is to try a
# connection.
# Currently we can't do that.
#
# The current solution handle two cases.
# The first case is when we only need one debug agent. This is the usual case. The default port
# (2345) is always used. In that case, we only check that the debug agent is running on the device
# without knowing if it listens on the given port or another one.
#
# The second case is when we need several debug agents. The use case is to be able to have several
# zxdb connected to several debug agents. In that case, each debug agent needs a different port.
# To check that the debug agent associated with a given port is running, we check that the port is
# open on the device. This only works is the ports are not forwarded by the local computer.
check_for_agent() {
  local port="$1"
  local num_tries="$2"

  while ((num_tries--)); do
    if [[ ${port} -eq "2345" ]]; then
      # This test works even when ports are forwarded but it only checks that one agent is running.
      if fx-command-run "shell" "ps" | grep "debug_agent.cmx" > /dev/null; then
        return 0
      fi
    else
      # This test only works when the device is local. It checks that the port is listening.
      if nc -w5 -z "$(get-fuchsia-device-addr)" "${port}"; then
        return 0
      fi
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
  if check_for_agent "${port}" 1; then
    echo "Found agent already running."
    return 0
  fi

  # Leave the SSH connection open. Will be closed on script end.
  # We branch out on whether the user used the verbose-agent flag. If so, we
  # redirect the debug agent output to /dev/null.
  echo -e "Debug agent not found. Starting one."
  fx-command-run "shell" "run" "${debug_agent_url}" "--port=${port}" "${unwind_flag}" > "${agent_out}" 2>&1 &

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
