#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# The bash_test_framework.sh script provides two ways to define a mock equivalent
# of a required executable: call 'btf::make_mock', or add the mocked executable path
# to the BT_MOCKED_TOOLS array in the host test script. This script provides the
# mock implementation, and provides options to tailor the behavior, results, and/or
# side effects when the mocked executable is invoked.
#
# When invoked, instead of running the real program, this program writes its state
# (such as, the arguments passed to the command) such that 'source'ing the state
# file will make the state information available to the caller.
#
# The mock state data (script) is written to a file with the same path as the script
# plus (if executed only once) a ".mock_state" extension (for example,
# "${executable_path}.mock_state"); however, if the mock executable is executed more
# than once, multiple files will be written, with the extension ".mock_state.<index>"
# (with index starting at "1", for example, "${executable_path}.mock_state.1",
# "${executable_path}.mock_state.2").
#
# After sourcing the ".mock_state(.n)" file, an array variable, "BT_MOCK_ARGS", will be
# set (or overwritten if a prior value was set) to the command name (the ${BASH_SOURCE})
# followed by the arguments passed to the mocked tool by the last caller. Also, if a
# ".mock_side_effects" file was provided, the variable "BT_SIDE_EFFECT_STATUS" will be
# set to the status returned from sourcing the ".mock_side_effects" file.
#
# Also (simply for convenience), if a "${executable_path}.mock_status" file was present,
# sourcing the "${executable_path}.mock_state(.n)" script will also result in the same
# status returned to the original caller.
#
# To generate a return status other than 0 (success), write the desired status int
# value to "${executable_path}.mock_status" before executing the mock.
#
# To generate a stdout result, similarly, write the desired output to
# "${executable_path}.mock_stdout" before executing the mock; and to generate stderr
# output, write the desired stderr output to "${executable_path}.mock_stderr".
#
# Additional side effects (actions to be taken by the mock script that have some
# actual effect, such as creating a file, or running another program) can be
# executed as well. Write desired actions in bash syntax to
# "${executable_path}.mock_side_effects", to be executed by 'source'ing the file.
# Any and all arguments passed to the mocked executable are forwarded to the
# sourced mock_side_effects script.
#
# Side effects run after writing stdout and stderr, allowing for a possible side
# effect that you may want the mock program to run forever (such as an infinite
# loop with a long sleep). Alternatively, your side effect program can write
# its own output.
#
# Limitations:
#   - Input from stdin is ignored. The only way to change the behavior is to
#     create the .mock_status, .mock_stdout, and/or .mock_stderr files.
#   - stdout results are written first, in entirety, followed by stderr results
#     (if supplied)

if [[ -e "${BASH_SOURCE}.mock_stdout" ]]; then
  cat "${BASH_SOURCE}.mock_stdout"
fi

if [[ -e "${BASH_SOURCE}.mock_stderr" ]]; then
  >&2 cat "${BASH_SOURCE}.mock_stderr"
fi

declare had_side_effect=false
declare -i side_effect_status=0
if [[ -e "${BASH_SOURCE}.mock_side_effects" ]]; then
  source "${BASH_SOURCE}.mock_side_effects" "$@"
  side_effect_status=$?
  had_side_effect=true
fi

declare -i status=0
if [[ -e "${BASH_SOURCE}.mock_status" ]]; then
  status=$(cat "${BASH_SOURCE}.mock_status")
elif ${had_side_effect}; then
  status=${side_effect_status}
fi

declare state_file="${BASH_SOURCE}.mock_state"
if [[ -e "${state_file}" ]]; then
  # Command was executed more than once. Use numeric suffixes.
  mv "${state_file}" "${state_file}.1"
  state_file="${state_file}.2"
elif [[ -e "${state_file}.1" ]]; then
  declare -i index
  declare -i max_index=1
  for file in $(ls "${state_file}".*); do
    index=${file##*.}
    max_index=$(( index > max_index ? index : max_index ))
  done
  state_file="${state_file}.$((max_index+1))"
fi

echo "#!/bin/bash" >>"${state_file}"
echo "BT_MOCK_ARGS=(\"${BASH_SOURCE}\" \"$@\")" >>"${state_file}"
if ${had_side_effect}; then
  echo "declare -i BT_MOCK_SIDE_EFFECT_STATUS=${side_effect_status}" >>"${state_file}"
fi
echo "return ${status}" >>"${state_file}"

# If script was sourced, use 'return', otherwise use 'exit'
(return 0 2>/dev/null) && return ${status} || exit ${status}
