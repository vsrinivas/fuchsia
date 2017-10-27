# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

### zset: set zircon build properties

if [[ -z "${FUCHSIA_DIR}" ]]; then
  source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/lib/vars.sh
fi

# __patched_path <old-regex> <new-component>
# Prints a new path value based on the current $PATH, removing path components
# that match <old-regex> and adding <new-component> to the end.
function __patched_path {
    local old_regex="$1"
    local new_component="$2"
    local stripped
    # Put each PATH component on a line, delete any lines that match the regex,
    # then glue back together with ':' characters.
    stripped="$(
        set -o pipefail &&
        echo "${PATH}" |
        tr ':' '\n' |
        grep -v -E "^${old_regex}$" |
        tr '\n' ':'
    )"
    # The trailing newline will have become a colon, so no need to add another
    # one here.
    echo "${stripped}${new_component}"
}

### fupdate-path: add useful tools to the PATH

# Add tools to path, removing prior tools directory if any. This also
# matches the Zircon tools directory added by zset, so add it back too.
function fupdate-path {
  local rust_dir="$(source "${FUCHSIA_DIR}/buildtools/vars.sh" && echo -n "${BUILDTOOLS_RUST_DIR}/bin")"

  export PATH="$(__patched_path \
      "${FUCHSIA_OUT_DIR}/[^/]*-[^/]*/tools" \
      "${FUCHSIA_BUILD_DIR}/tools:${ZIRCON_TOOLS_DIR}"
  )"

  export PATH="$(__patched_path "${rust_dir}" "${rust_dir}")"
}
