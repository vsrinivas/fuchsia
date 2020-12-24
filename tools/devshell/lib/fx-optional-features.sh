#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

function list_optional_features {
  echo \
    "incremental" \
    "ffx_discovery"
}

# Return 0 (true) if the default of the given feature is enabled, 1 (false) otherwise.
function is_feature_enabled_by_default {
  case "$1" in
  "incremental") return 1 ;;
  "ffx_discovery") return 0 ;;
  esac

  # global default is enabled
  return 0
}

_FX_INDENT="      "
function help_optional_feature {
  local h=()
  case "$1" in
  "incremental")
    h=(
      "Packages are published and served incrementally by 'fx serve-updates'"
      "as they are built. Explicit 'fx build' is not required for most operations."
      )
    ;;
  "ffx_discovery")
    h=(
      "Device discovery based on ffx instead of device-finder."
      )
    ;;
  esac
  printf "${_FX_INDENT}${_FX_INDENT}%s\n" "${h[@]}"
}

function help_optional_features {
  for el in $(list_optional_features); do
    local default current
    is_feature_enabled_by_default $el && default="enabled" || default="disabled"
    is_feature_enabled $el && current="enabled" || current="disabled"
    echo "${_FX_INDENT}${el}"
    echo "${_FX_INDENT}${_FX_INDENT}(default=$default, current=$current)"
    help_optional_feature $el
  done
}

function warn_if_feature_enabled {
  local feature
  feature="$1"
  if is_feature_enabled "$feature"; then
    echo "Feature \"$feature\" is enabled."
  fi
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

function get_fx_flags_non_default_features {
  local out=()
  for el in $(list_optional_features); do
    local default=false
    local current=false
    is_feature_enabled_by_default $el && default=true
    is_feature_enabled $el && current=true
    if $default && ! $current; then
      out+=("--disable=$el")
    elif ! $default && $current; then
      out+=("--enable=$el")
    fi
  done
  echo "${out[@]}"
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
  if ! is_valid_feature "${feature}"; then
    return 1
  fi
  local disabled_env
  disabled_env="$(get_disable_feature_env_name "${feature}")"

  # if ${disabled_env} is not set, use the default
  if [[ -z ${!disabled_env+x} ]]; then
    is_feature_enabled_by_default "$feature"
  else
    test "${!disabled_env}" != "1"
  fi
}
