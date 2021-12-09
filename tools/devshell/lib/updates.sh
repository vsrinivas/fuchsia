# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

function check-for-package-server {
  if is_feature_enabled "legacy_serve"; then
    # Make sure it is running.
    if [[ -z "$(pgrep -f 'pm serve .*/amber-files')" ]]; then
      fx-error "It looks like serve-updates is not running."
      fx-error "You probably need to start \"fx serve\""
      return 1
    fi

    # Warn if it is using the wrong repository.
    if [[ -z "$(pgrep -f "pm serve .*${FUCHSIA_BUILD_DIR}/amber-files")" ]]; then
      fx-warn "WARNING: It looks like serve-updates is running in a different workspace."
      fx-warn "WARNING: You probably need to stop that one and start a new one here with \"fx serve\""
    fi

    # Warn if incremental is enabled for this shell, but the server is not auto publishing packages.
    if is_feature_enabled "incremental"; then
      # Regex terminates with a space to avoid matching the -persist option.
      if [[ -z "$(pgrep -f "pm serve .*${FUCHSIA_BUILD_DIR}/amber-files .*-p ")" ]]; then
        fx-warn "WARNING: Incremental build is enabled, but it looks like incremental build is disabled for serve-updates."
        fx-warn "WARNING: You probably need to stop serve-updates, and restart it with incremental build enabled."
        fx-warn "WARNING: You can enable incremental build in the shell running serve-updates with 'export FUCHSIA_DISABLED_incremental=0'"
      fi
    fi
  else
    if [[ "$(uname -s)" == "Darwin" ]]; then
      if ! netstat -anp tcp | awk '{print $4}' | grep "\.8085$" > /dev/null; then
        fx-error "It looks like the ffx package server is not running."
        fx-error "You probably need to run \"fx add-update-source\""
        return 1
      fi
    else
      if ! ss -f inet -f inet6 -an | awk '{print $5}' | grep ":8085$" > /dev/null; then
        fx-error "It looks like the ffx package server is not running."
        fx-error "You probably need to run \"fx add-update-source\""
        return 1
      fi
    fi

    # FIXME(http://fxbug.dev/80431): Check if the current `devhost` points at
    # '${FUCHSIA_BUILD_DIR}/amber-files'.
  fi

  return 0
}

function ffx-default-repository-name {
    # Use the build directory's name by default. Note that package URLs are not
    # allowed to have underscores, so replace them with hyphens.
    basename "${FUCHSIA_BUILD_DIR}" | tr '_' '-'
}

function ffx-add-repository {
  local repo_name="$1"
  shift

  if [[ -z "$repo_name" ]]; then
    fx-error "The repository name was not specified"
    return 1
  fi

  fx-command-run ffx --config ffx_repository=true repository add-from-pm \
    --repository "$repo_name" \
    "${FUCHSIA_BUILD_DIR}/amber-files"
  err=$?
  if [[ $err -ne 0 ]]; then
    fx-error "The repository was not able to be added to ffx"
    return $err
  fi

  return 0
}

function ffx-register-repository {
  local repo_name="$1"
  shift

  ffx-add-repository "$repo_name" || return $?

  fx-command-run ffx --config ffx_repository=true target repository register \
    --repository "$repo_name" \
    --alias "fuchsia.com" \
    "$@"
  err=$?
  if [[ $err -ne 0 ]]; then
    fx-error "The repository was unable to be added to the target device"
    return $err
  fi

  return 0
}

function ffx-repository-server-address {
  local addr=$(fx-command-run ffx config get repository.server.listen)
  err=$?
  if [[ $err -ne 0 ]]; then
    fx-error "Unable to get the configured repository server address."
    return "$err"
  fi

  if [[ "$addr" = "null" ]]; then
    echo ""
  else
    echo "$addr" | fx-command-run jq -r .
  fi

  return 0
}

function ffx-repository-check-server-address {
  local expected_ip="$1"
  local expected_port="$2"

  local actual_addr=$(ffx-repository-server-address)
  local err=$?
  if [[ $err -ne 0 ]]; then
    return "$err"
  fi

  if [[ -z "${actual_addr}" ]]; then
    fx-error "repository server is currently disabled. to re-enable, run:"
    fx-error "$ ffx config set repository.server.listen \"[::]:8083\" && ffx doctor --restart-daemon"
    return 1
  fi

  if [[ $actual_addr =~ (.*):([0-9]+) ]]; then
    local actual_ip="${BASH_REMATCH[1]}"
    local actual_port="${BASH_REMATCH[2]}"
  else
    fx-error "could not parse ip and port from ffx server address: $actual_addr"
    return 1
  fi

  if [[ -z "$expected_ip" ]]; then
    expected_ip="$actual_ip"
  elif [[ $expected_ip =~ : ]]; then
    expected_ip="[$expected_ip]"
  fi

  if [[ -z "$expected_port" ]]; then
    expected_port="$actual_port"
  fi

  local expected_addr="$expected_ip:$expected_port"

  if [[ "$expected_addr" != "$actual_addr" ]]; then
    fx-error "The repository server is configured to use \"${actual_addr}\", not \"${expected_addr}\""
    fx-error "To switch to a different address, run:"
    fx-error ""
    fx-error "$ ffx config set repository.server.listen \"${expected_addr}\" && ffx doctor --restart-daemon"
    fx-error ""
    fx-error "Note: this will change the address for all repositories served by ffx"
    return 1
  fi

  return 0
}
