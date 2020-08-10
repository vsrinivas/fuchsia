# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

## usage: . scripts/fx-env.sh [--bash-build-completions]
##
## optional arguments:
##   --bash-build-completion  Enables BASH completion support for the `fx build`
##                            command. See //scripts/gn_complete/README.md for
##                            other requirements.

### NOTE!
###
### This is not a normal shell script that executes on its own.
###
### It's evaluated directly in a user's interactive shell using
### the `.` or `source` shell built-in.
###
### Hence, this code must be careful not to pollute the user's shell
### with variable or function symbols that users don't want.

function __fx_env_main() {
  local bash_build_completion=false
  for arg in "$@"; do
    if [[ "${arg}" == "--bash-build-completion" ]]; then
      bash_build_completion=true
    fi
  done

  if [[ -n "${ZSH_VERSION}" ]]; then
    export FUCHSIA_DIR="$(cd "$(dirname "${(%):-%x}")/.." >/dev/null 2>&1 && pwd)"
  else
    export FUCHSIA_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd)"
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
    local jiri_bin_dir="${FUCHSIA_DIR}/.jiri_root/bin"
    export PATH="$(__patched_path "${jiri_bin_dir}" "${jiri_bin_dir}")"

    export PATH="$(__patched_path "${FUCHSIA_DIR}/scripts/git" "${FUCHSIA_DIR}/scripts/git")"

    local rust_dir="$(source "${FUCHSIA_DIR}/tools/devshell/lib/vars.sh" && echo -n "${PREBUILT_RUST_DIR}/bin")"
    export PATH="$(__patched_path "${rust_dir}" "${rust_dir}")"

    # XXX(raggi): these can get stale, so this really needs rework. Probably the
    # only way to handle this more correctly is to wrap fx in a function that
    # re-runs fx-update-path whenever `fx set` or `fx use` succeed.
    local fuchsia_tools_dir="$(fx-config-read 2>/dev/null; echo "${FUCHSIA_BUILD_DIR}/tools")"
    local zircon_tools_dir="$(fx-config-read 2>/dev/null; echo "${ZIRCON_TOOLS_DIR}")"

    if [[ -n "${fuchsia_tools_dir}" ]]; then
      export PATH="$(__patched_path \
        "${FUCHSIA_OUT_DIR}/[^/]*/tools" \
        "${fuchsia_tools_dir}")"
    fi
    if [[ -n "${zircon_tools_dir}" ]]; then
      export PATH="$(__patched_path \
        "${FUCHSIA_OUT_DIR}/[^/]*/tools" \
        "${zircon_tools_dir}")"
    fi
  }

  ### fx-prompt-info: prints the current configuration for display in a prompt

  function fx-prompt-info {
    if [[ ${PWD}/ = ${FUCHSIA_DIR}/* ]]; then
      # Run in a subshell to avoid polluting this shell's environment with data
      # from the config file, which can change without updating this shell's
      # environment.
      (
        source "${FUCHSIA_DIR}/tools/devshell/lib/vars.sh"
        if fx-build-dir-if-present; then
          echo "${FUCHSIA_BUILD_DIR##*/}"
        else
          echo "???"
        fi
      )
    fi
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

  ### fd: navigate to directories with autocomplete

  # $ fd --help   # for usage.
  function fd {
    local fd_python
    local dest
    fd_python="${FUCHSIA_DIR}/scripts/fd.py"
    dest=$(eval ${fd_python} "$@")
    cd -- "${dest}"
  }

  if [[ -z "${ZSH_VERSION}" ]]; then
    function __fd {
      local cur
      COMPREPLY=()
      cur="${COMP_WORDS[COMP_CWORD]}"
      if [[ ${cur:0:2} == "//" ]]; then
        COMPREPLY=($(/bin/ls -dp1 ${FUCHSIA_DIR}/${cur}* 2>/dev/null | \
          sed -n "s|^${FUCHSIA_DIR}/\(.*/\)\$|\1|p" | xargs echo))
      else
        COMPREPLY=($(/bin/ls -dp1 ${cur}* 2>/dev/null | grep "/$" | xargs echo))
      fi
    }
    complete -o nospace -F __fd fd
  fi

  ### fx-go: alias of fd, for backward compatibility

  function fx-go {
    echo "fx-go is to be deprecated in Q1 2018 in favor of 'fd'. For more help, fd --help"
    fd "$@"
  }


  # Support command-line auto-completions for the fx command.
  if [[ -z "${ZSH_VERSION}" ]]; then

    function __fx_complete_cmd {
      local cmd cur prev
      cmd="${COMP_WORDS[1]}"
      cur="${COMP_WORDS[COMP_CWORD]}"
      prev="${COMP_WORDS[COMP_CWORD-1]}"
      case "${cmd}" in
        set)
          if [[ ${COMP_CWORD} -eq 2 ]]; then
            __fx_set_compreply_for_product_board "${cur}"
            return
          fi
          ;;

        vendor)
          if [[ ${COMP_CWORD} -eq 2 ]]; then
            # return only vendors that have vendor/*/scripts/devshell/*
            COMPREPLY=()
            for v in "${FUCHSIA_DIR}"/vendor/"${cur}"*/scripts/devshell; do
              v=${v##"${FUCHSIA_DIR}/vendor/"}
              v=${v%%"/scripts/devshell"}
              COMPREPLY+=("$v")
            done
            return

          elif [[ ${COMP_CWORD} -eq 3 ]]; then
            COMPREPLY=()
            for file in "${FUCHSIA_DIR}"/vendor/"${prev}"/scripts/devshell/"${cur}"*; do
              if [[ -x "${file}" ]]; then
                COMPREPLY+=("${file##*/}")
              fi
            done
            return
          fi
          ;;

        build)
          if [[ ${COMP_CWORD} -eq 2 ]]; then
            __fx_complete_build "${cur}"
          fi
          ;;
      esac
    }

    # Only define the completion function for `fx build` if requested.
    if $bash_build_completion; then
      function __fx_complete_build {
        local target_py="${FUCHSIA_DIR}/scripts/gn_complete/complete.py"
        # In a subshell, so as not to pollute the user's environment,
        # load the build environment variables and execute the completion
        # script.
        COMPREPLY=(
          $(
            source "${FUCHSIA_DIR}/tools/devshell/lib/vars.sh"
            fx-config-read
            "${target_py}" "${1}" 2> /dev/null
          )
        )
      }
    else
      function __fx_complete_build { :; }
    fi

    function __fx_set_compreply_for_product_board {
      local prefix=$1
      if [[ "${prefix}" =~ \. ]]; then
        # product is filled, find a board
        local product="${prefix%%\.*}"
        prefix="${prefix##*\.}"
        for file in "${FUCHSIA_DIR}"/{.,vendor/*}/boards/"${prefix}"*.gni*; do
          if [[ -f "${file}" ]]; then
            file="${file##*/}"
            COMPREPLY+=("${product}.${file%%".gni"}")
          fi
        done
      else
        # find a product
        if [[ $(type -t compopt) == 'builtin' ]]; then
          compopt -o nospace
        fi
        for file in "${FUCHSIA_DIR}"/{.,vendor/*}/products/"${prefix}"*.gni*; do
          if [[ -f "${file}" ]]; then
            file="${file##*/}"
            COMPREPLY+=("${file%%".gni"}")
          fi
        done
        if [[ ${#COMPREPLY[@]} -eq 1 ]]; then
          COMPREPLY=("${COMPREPLY[0]}.")
        fi
      fi
    }

    function __fx {
      local fuchsia_tools_dir="$(fx-config-read 2>/dev/null; echo "${FUCHSIA_BUILD_DIR}/tools")"
      COMPREPLY=()
      if [[ ${COMP_CWORD} -eq 1 ]]; then
        local files cmd
        cmd="${COMP_WORDS[1]}"
        files=("${FUCHSIA_DIR}"/tools/devshell/"${cmd}"* "${FUCHSIA_DIR}"/tools/devshell/contrib/"${cmd}"*)
        if [[ -d "${fuchsia_tools_dir}" ]]; then
          files+=("${fuchsia_tools_dir}"/"${cmd}"*)
        fi
        for file in "${files[@]}"; do
          if [[ -x "${file}" ]]; then
            COMPREPLY+=("${file##*/}")
          fi
        done
      else
        __fx_complete_cmd
      fi
    }
    complete -o default -F __fx fx
  fi
}

__fx_env_main "$@"
unset __fx_env_main
