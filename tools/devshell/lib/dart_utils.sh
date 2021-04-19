# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# The functions here are used to compile Dart commands, but may be generalized
# later to support compiling other tools if needed. This functionality is also
# covered by host-tools code, but host-tools doesn't correctly generate the
# metadata entries in //out/default/tool_paths.json for Dart binaries.

# Build the tool, but only if necessary.
function _build-if-necessary {
  local target="$1"
  local executable="$2"

  # It is only necessary to rebuild if the binary isn't present. It is rebuilt
  # as part of an `fx build` so always up-to-date.
  if [[ ! -f "${executable}" ]]; then
    fx-info "Building required tool \"${target}\"..."
    if ! fx-command-run build "${target}"; then
      fx-error "Could not compile ${executable##*/}"
      return 1
    fi
  fi
}

# Run a dart tool, checking first if it needs to be built. The calling script
# must call `fx-config-read` before calling this function because it depends on
# HOST_OUT_DIR.
function run-dart-tool {
  local tool_name="$1"
  shift

  local executable="${HOST_OUT_DIR}/dart-tools/${tool_name}"
  local tool_build_target="${HOST_OUT_DIR##*/}/dart-tools/${tool_name}"

  _build-if-necessary "${tool_build_target}" "${executable}"

  exec "${executable}" "$@"
}
