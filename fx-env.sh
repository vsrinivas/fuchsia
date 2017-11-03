# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ -n "${ZSH_VERSION}" ]]; then
  source "$(cd "$(dirname "${(%):-%x}")" && pwd)"/devshell/lib/vars.sh
else
  source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/devshell/lib/vars.sh
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

### fx-update-path: add useful tools to the PATH

# Add tools to path, removing prior tools directory if any. This also
# matches the Zircon tools directory added by zset, so add it back too.
function fx-update-path {
  local rust_dir="$(source "${FUCHSIA_DIR}/buildtools/vars.sh" && echo -n "${BUILDTOOLS_RUST_DIR}/bin")"

  local build_dir="$(fx-config-read; echo "${FUCHSIA_BUILD_DIR}")"

  local tools_dirs="${ZIRCON_TOOLS_DIR}"
  if [[ -n "${build_dir}" ]]; then
    tools_dirs="${build_dir}/tools:${tools_dirs}"
  fi

  export PATH="$(__patched_path \
      "${FUCHSIA_OUT_DIR}/[^/]*-[^/]*/tools" \
      "${tools_dirs}"
  )"

  export PATH="$(__patched_path "${rust_dir}" "${rust_dir}")"
}

### fx-prompt-info: prints the current configuration for display in a prompt

function fx-prompt-info {
  # Run fx-config-read in a subshell to avoid polluting this shell's environment
  # with data from the config file, which can change without updating this
  # shell's environment.
  (
    fx-config-read
    echo "${FUCHSIA_ARCH}-${FUCHSIA_VARIANT}"
  )
}

### fx-set-prompt: displays the current configuration in the prompt

function fx-set-prompt {
  if [[ -n "${ZSH_VERSION}" ]]; then
    autoload -Uz colors && colors
    setopt PROMPT_SUBST
    export PS1='%B%F{yellow}[$(fx-prompt-info)] %B%F{blue}%m:%~%#%f%b '
    export PS2='%B%F{blue}>%f%b '
  else
    export PS1='\[\e[0;1;33m\][$(fx-prompt-info)] \[\e[34m\]\h:\w\\$\[\e[0m\] '
    export PS2='\[\e[0;1;34m\]>\[\e[0m\] '
  fi
}

### fx-go: navigate to directory within Fuchsia

function fx-go-usage {
  cat <<END
usage: fgo [dir]
Navigates to directory within fuchsia root.
END
}

function fx-go {
  if [[ $# -gt 1 ]]; then
    fx-go-usage
    return 1
  fi

  cd "${FUCHSIA_DIR}/$1"
}

if [[ -z "${ZSH_VERSION}" ]]; then
  function __fx_go {
    local cur
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    COMPREPLY=($(/bin/ls -dp1 ${FUCHSIA_DIR}/${cur}* 2>/dev/null | \
      sed -n "s|^${FUCHSIA_DIR}/\(.*/\)\$|\1|p" | xargs echo))
  }
  complete -o nospace -F __fx_go fx-go
fi
