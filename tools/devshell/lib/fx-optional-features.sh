#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

function list_optional_features {
  echo "incremental" "legacy_discovery"
}

function is_valid_feature {
  local el feature
  feature="$1"
  for el in $(list_optional_features); do
    if [[ "${el}" == "${feature}" ]]; then
      return 0
    fi
  done
  return 1
}

function get_disable_feature_env_name {
  local feature
  feature="$1"
  echo "FUCHSIA_DISABLED_${feature}"
}

# Return code 0 (true) if the given feature is enabled, 1 (false) otherwise.
function is_feature_enabled {
  local feature
  feature="$1"
  if is_valid_feature "${feature}"; then
    local env_name="$(get_disable_feature_env_name "${feature}")"
    [[ "${!env_name}" != "1" ]] && return 0
  fi
  return 1
}
