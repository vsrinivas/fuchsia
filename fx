#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

function usage {
  cat <<END
usage: fx [--config CONFIG] COMMAND [...]

Run Fuchsia development commands. Must be run with a current working directory
that is contained in a Fuchsia source tree.

commands:
$(ls "${fuchsia_dir}/scripts/devshell" | grep -v lib | sed -e 's/^/  /')

optional arguments:
  --config              Path to the config file use when running COMMAND.
                        Defaults to FUCHSIA_CONFIG if set in the
                        environment and "${FUCHSIA_DIR}/.config" otherwise.
END
}

# We walk the parent directories looking for .jiri_manifest rather than using
# BASH_SOURCE so that we find the fuchsia_dir enclosing the current working
# directory instead of the one containing this file in case the user has
# multiple Fuchsia source trees and is picking up this file from another one.
fuchsia_dir="$(pwd)"
while [[ ! -f "${fuchsia_dir}/.jiri_manifest" ]]; do
  fuchsia_dir="$(dirname "${fuchsia_dir}")"
  if [[ "${fuchsia_dir}" == "/" ]]; then
    echo >& 2 "error: Cannot find Fuchsia source tree containing $(pwd)"
    exit 1
  fi
done

if [[ "$1" == "--config" ]]; then
  if [[ $# -lt 2 ]]; then
    usage
    echo >& 2 "error: Missing path to config file for --config argument"
    exit 1
  fi
  shift # Removes --config.
  export FUCHSIA_CONFIG="$1"
  shift # Removes the path to the config file.
fi

if [[ $# -lt 1 ]]; then
  usage
  echo >& 2 "error: Missing command name"
  exit 1
fi

command_name="$1"

# The "help" command is built-in and just prints the usage.
#
# Rather than adding more built-in commands, please add separate scripts in the
# "devshell" directory.
if [[ "$command_name" == "help" ]]; then
  usage
  exit 0
fi

command_path="${fuchsia_dir}/scripts/devshell/${command_name}"

if [[ ! -f "${command_path}" ]]; then
  usage
  echo >& 2 "error: Unknown command ${command_name}"
  exit 1
fi

shift # Removes the command name.
"${command_path}" "$@"
