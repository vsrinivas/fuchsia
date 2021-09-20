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
    "$repo_name" \
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
