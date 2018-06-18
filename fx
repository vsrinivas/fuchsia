#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

function get_build_dir {
  (source "${vars_sh}" && fx-config-read-if-present && echo "${FUCHSIA_BUILD_DIR}")
}

function commands {
  local cmds="$(ls "${fuchsia_dir}/scripts/devshell" | grep -v -e '^lib$' -e '^tests$')"

  local newline=$'\n'
  local build_dir=$(get_build_dir)
  if [[ -n "${build_dir}" ]]; then
    cmds="${cmds}${newline}$(ls "${build_dir}/tools" 2>/dev/null)"
  fi

  for tool in ${buildtools_whitelist}; do
    cmds="${cmds}${newline}${tool}"
  done

  echo "$(echo "${cmds}" | sort)"
}

function find_command {
  local cmd=$1

  local command_path="${fuchsia_dir}/scripts/devshell/${cmd}"
  if [[ -x "${command_path}" ]]; then
    echo "${command_path}"
    return 0
  fi

  local build_dir=$(get_build_dir)
  if [[ -n "${build_dir}" ]]; then
    local command_path="${build_dir}/tools/${cmd}"
    if [[ -x "${command_path}" ]]; then
      echo "${command_path}"
      return 0
    fi
  fi

  for tool in ${buildtools_whitelist}; do
    if [[ "$cmd" != "$tool" ]]; then
      continue
    fi
    local command_path="${fuchsia_dir}/buildtools/${tool}"
    if [[ -x "${command_path}" ]]; then
      echo "${command_path}"
      return 0
    fi
  done

  return 1
}

function help {
  local cmd="$1"
  if [[ -z "${cmd}" ]]; then
    for cmd in $(commands); do
      local cmd_path="$(find_command "${cmd}")"
      if [[ $(file -b --mime "${cmd_path}" | cut -d / -f 1) == "text" ]]; then
        echo "${cmd} | $(sed -n '1,/^###/s/^### //p' < "${cmd_path}")"
      else
        echo "${cmd}"
      fi
    done | column -t -s '|' -c 2
  else
    local cmd_path="$(find_command "${cmd}")"
    if [[ -z "$cmd_path" ]]; then
      echo "Command not found"
    elif [[ $(file -b --mime "${cmd_path}" | cut -d / -f 1) == "text" ]]; then
      fx-print-command-help "${cmd_path}"
    else
      echo "No help found. Try \`fx ${cmd} -h\`"
    fi
  fi
}

function usage {
  cat <<END
usage: fx [--config CONFIG_FILE | --dir BUILD_DIR] [-i] [-x] COMMAND [...]

Run Fuchsia development commands. Must be run with either a current working
directory that is contained in a Fuchsia source tree or the FUCHSIA_DIR
environment variable set to the root of a Fuchsia source tree.

commands:
$(help)

optional arguments:
  --config=CONFIG_FILE  Path to the config file use when running COMMAND.
                        Defaults to FUCHSIA_CONFIG if set in the
                        environment and //.config otherwise.  The config
                        file determines which build directory (and therefore
                        build configuration) is used by COMMAND.
  --dir=BUILD_DIR       Path to the build directory to use when running COMMAND.
                        If specified, FILE is ignored.
  -i                    Iterative mode.  Repeat the command whenever a file is
                        modified under your Fuchsia directory, not including
                        out/.
  -x                    Print commands and their arguments as they are executed.

optional shell extensions:
  fx-go
  fx-update-path
  fx-set-prompt

To use these shell extensions, first source fx-env.sh into your shell:

  $ source scripts/fx-env.sh

END
}

buildtools_whitelist="gn ninja"

fuchsia_dir="${FUCHSIA_DIR}"
if [[ -z "${fuchsia_dir}" ]]; then
  # We walk the parent directories looking for .jiri_root rather than using
  # BASH_SOURCE so that we find the fuchsia_dir enclosing the current working
  # directory instead of the one containing this file in case the user has
  # multiple Fuchsia source trees and is picking up this file from another one.
  fuchsia_dir="$(pwd)"
  while [[ ! -d "${fuchsia_dir}/.jiri_root" ]]; do
    fuchsia_dir="$(dirname "${fuchsia_dir}")"
    if [[ "${fuchsia_dir}" == "/" ]]; then
      echo >& 2 "error: Cannot find Fuchsia source tree containing $(pwd)"
      exit 1
    fi
  done
fi

declare -r vars_sh="${fuchsia_dir}/scripts/devshell/lib/vars.sh"
source "${vars_sh}"

while [[ $# -ne 0 ]]; do
  case $1 in
    --config=*|--dir=*)
      # Turn --switch=value into --switch value.
      arg="$1"
      shift
      set -- "${arg%%=*}" "${arg#*=}" "$@"
      continue
      ;;
    --config)
      if [[ $# -lt 2 ]]; then
        usage
        echo >&2 "ERROR: Missing path to config file for --config argument"
        exit 1
      fi
      shift # Removes --config.
      export FUCHSIA_CONFIG="$1"
      ;;
    --dir)
      if [[ $# -lt 2 ]]; then
        usage
        echo >&2 "ERROR: Missing path to build directory for --dir argument"
        exit 1
      fi
      shift # Removes --dir.
      export FUCHSIA_BUILD_DIR="$1"
      if [[ "$FUCHSIA_BUILD_DIR" == //* ]]; then
        FUCHSIA_BUILD_DIR="${fuchsia_dir}/${FUCHSIA_BUILD_DIR#//}"
      fi
      if [[ ! -d "$FUCHSIA_BUILD_DIR" ]]; then
        echo >&2 "ERROR: Build directory $FUCHSIA_BUILD_DIR does not exist"
        exit 1
      fi
      # This tells fx-config-read not to use the file.
      export FUCHSIA_CONFIG=-
      ;;
    -i)
      declare iterative=1
      ;;
    -x)
      export FUCHSIA_DEVSHELL_VERBOSITY=1
      ;;
    --)
      shift
      break
      ;;
    help)
      if [[ $# -gt 1 ]]; then
        shift
        help "$@"
        exit
      else
        usage
        exit 1
      fi
      ;;
    -*)
      usage
      echo >& 2 "error: Unknown global argument $1"
      exit 1
      ;;
    *)
      break
      ;;
  esac
  shift
done

if [[ $# -lt 1 ]]; then
  usage
  echo >& 2 "error: Missing command name"
  exit 1
fi

command_name="$1"
command_path="$(find_command ${command_name})"

if [[ $? -ne 0 ]]; then
  usage
  echo >& 2 "error: Unknown command ${command_name}"
  exit 1
fi

shift # Removes the command name.

if [ -z "${iterative}" ]; then
  "${command_path}" "$@"
elif which inotifywait >/dev/null; then
  # Watch everything except out/ and files/directories beginning with "."
  # such as lock files, swap files, .git, etc'.
  inotifywait -qrme modify --exclude "/\." "${fuchsia_dir}" @"${fuchsia_dir}"/out | while read; do
    # Drain all subsequent events in a batch.
    # Otherwise when multiple files are changes at once we'd run multiple
    # times.
    read -d "" -t .01
    # Allow at most one fx -i invocation per Fuchsia dir at a time.
    # Otherwise multiple concurrent fx -i invocations can trigger each other
    # and cause a storm.
    "${command_path}" "$@"
  done
elif which apt-get >/dev/null; then
  echo "Missing inotifywait"
  echo "Try: sudo apt-get install inotify-tools"
elif which fswatch >/dev/null; then
  fswatch --one-per-batch --event=Updated -e "${fuchsia_dir}"/out/ -e "/\." . | while read; do
    "${command_path}" "$@"
  done
else
  echo "Missing fswatch"
  echo "Try: brew install fswatch"
fi
