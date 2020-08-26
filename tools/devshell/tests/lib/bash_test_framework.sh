#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# The Bash Test Framework
#
# OVERVIEW
#
# The Bash Test Framework makes it easy to write tests that exercise Bash
# scripts.
#
# RUNNING TESTS
#
# The bash_test_framework.sh library script does not depend on Fuchsia's
# environment, but the "fx self-test" command is a convenient way to
# launch a test from the //tools/devshell/tests directory. For example:
#
#    $ fx self-test <host_test_script>
#
# The "self-test" command automatically loads the
# bash_test_framework.sh library and then runs the host test script.
#
# HOW THE FRAMEWORK EXECUTES YOUR TESTS
#
# By default, the framework finds all functions beginning with "TEST_..." and
# runs them in the order they were declared. (The default subset and/or order
# of test function to run can be set manually by declaring the list of test
# functions in the bash arrayh variable "BT_TEST_FUNCTIONS".)
#
# For each "TEST_..." function, the framework creates a pseudo-sandbox
# environment and relaunches the test script to run the test function, then
# tears down the sandbox, and creates a new sandbox to run the next test
# function. More specifically, for each "TEST_..." function, the framework:
#
#   1. Creates a temporary directory and copies the test script and the bash
#      framework to that directory.
#   2. Creates the directories listed in the array BT_MKDIR_DEPS within the
#      temp directory. The framework creates subdirectories within the paths.
#   3. Copies the files listed in BT_FILE_DEPS to the same relative locations in
#      the temp directory, creating any additional intermediate directories if
#      required.
#   4. Creates symbolic links for files and directories listed in
#      BT_LINKED_DEPS to the same relative locations in
#      the temp directory, creating any additional intermediate directories if
#      required.
#   5. Copies the mock.sh mock executable script to the tool subpaths lised in
#      BT_MOCKED_TOOLS.
#   6. Calls BT_INIT_TEMP_DIR() (if present).
#   7. Launches the sandboxed subprocess (a new sub-shell without user-specific
#      settings)
#   8. Then calls BT_SET_UP() (if present), the current "TEST_..." function,
#      and BT_TEAR_DOWN() (if present).
#
# CONVENIENCE FUNCTIONS
#
# The framework provides conveninece functions in the style of other testing
# frameworks and summarizes test success/failure upon completion. Functions prefixed
# with BT_EXPECT_... increment a counter for each failed test but continue executing
# the remaining tests in the test function. If one or more BT_EXPECT_... tests fail,
# The entire test function fails. Functions prefixed with BT_ASSERT_... abort the
# test function from that point, and increment the failed test counter, also failing
# the entire test function.
#
# Utility functions start with "btf::", such as "btf::error", "btf::failed", or
# "btf::stderr".
#
# EXAMPLES:
#
# Imagine a script to test is at "tools/prefs.sh":
#---------------------------------------------------------------------------------------------------
#   #!/bin/bash
#
#   # Some other set of functions the script depends on.
#   source lib/file_utils.sh
#
#   create_prefs_file() {
#     bin/formatter $2 > "${FUCHSIA_DIR}/out/default/$1"
#   }
#---------------------------------------------------------------------------------------------------
#
# A test for this script, such as "tools/devshell/test/prefs_test", may look something like:
#---------------------------------------------------------------------------------------------------
#   #!/bin/bash
#
#   BT_FILE_DEPS=("lib/file_utils.sh")
#   BT_MOCKED_TOOLS=("bin/formatter")
#   BT_MKDIR_DEPS=("out/default")
#
#   BT_INIT_TEMP_DIR() {
#     cat > bin/formatter.mock_side_effects <<EOF
#       echo $1
#     EOF
#   }
#
#   BT_SET_UP() {
#     if [[ "${FUCHSIA_DIR}" == "" ]]; then
#       FUCHSIA_DIR="${BT_TEMP_DIR}"
#       source "${FUCHSIA_DIR}/tools/prefs.sh"
#     fi
#   }
#
#   TEST_create_prefs_file {
#     BT_ASSERT_FUNCTION_EXISTS create_prefs_file
#     local -r content="config=DEBUG"
#     create_prefs_file .config "${content}"
#     BT_EXPECT_FILE_CONTAINS "${FUCHSIA_DIR}/out/default/.config" "${content}"
#   }
#
#   BT_RUN_TESTS "$@"
#---------------------------------------------------------------------------------------------------
#
# ANNOTATED EXAMPLE OF THE SAME TEST SCRIPT:
#---------------------------------------------------------------------------------------------------
#   #!/bin/bash
#
#   # The variable and function declarations prefixed by "BT_", shown below,
#   # are optional:
#
#   #######################################
#   # The root directory for your project source is normally inferred from the location
#   # of the bash_test_framework.sh script, but can be manually overridden by setting:
#   #######################################
#   # BT_DEPS_ROOT="/Your/Root/Dir".
#
#   #######################################
#   # Files (or entire subdirectories, recursively), to be copied to the isolated
#   # test directory. (Intermediate directories will be created automatically.)
#   #######################################
#   BT_FILE_DEPS=("lib/file_utils.sh")
#
#   #######################################
#   # Executables (binaries and/or scripts, including sourced scripts) to generated
#   # as a mock version of the executable. See "mock.sh". (Intermediate directories
#   # will be created automatically.)
#   #######################################
#   BT_MOCKED_TOOLS=("bin/formatter")
#
#   #######################################
#   # Any additional directories not already created as a result of one of the prior
#   # declarations.
#   #######################################
#   BT_MKDIR_DEPS=("out/default")
#
#   #######################################
#   # If declared, the BT_INIT_TEMP_DIR function is called by the framework,
#   # after staging all files, directories, and mock executables, and before
#   # relaunching the test script in its clean environment.
#   #
#   # Any additional initialization steps that require access to the original
#   # project source directory should be performed here, if needed.
#   #
#   # BT_TEMP_DIR will be set to the temporary root directory created to execute
#   # a single test.
#   # BT_DEPS_ROOT will be set to the root directory of the original project
#   # source directory path.
#   #
#   # The current working directory will be set to the root of the new temporary
#   # directory (BT_TEMP_DIR).
#   #
#   # No variables (exported or otherwise) set from this script will propagate
#   # to test functions.
#   #######################################
#   BT_INIT_TEMP_DIR() {
#     cat > bin/formatter.mock_side_effects <<EOF
#       echo $1
#     EOF
#   }
#
#   #######################################
#   # If declared, BT_SET_UP is called within the isolated test environment, just
#   # before invoking one of the test functions.
#   #
#   # Any initialization steps that do not require access to the original project
#   # source directory should be performed here, if needed.
#   #
#   # BT_TEST_ARGS is a bash array variable that may contain test-specific command
#   # line options passed to the test script after the argument '--', for example:
#   #
#   #   fx self-test <host_test_script> [--framework_options] -- [--test_options]
#   #
#   # BT_TEMP_DIR will be set to the temporary root directory created to execute
#   # a single test.
#   # BT_TEST_ARGS - array of command line arguments passed to the test script
#   # after the argument '--' (can be included at the end of 'fx self-test')
#   #
#   # The current working directory will be set to the directory containing the
#   # BT_SET_UP bash function (from within the temporary root directory).
#   #######################################
#   BT_SET_UP() {
#     if [[ "${FUCHSIA_DIR}" == "" ]]; then
#       FUCHSIA_DIR="${BT_TEMP_DIR}"
#       source "${FUCHSIA_DIR}/tools/prefs.sh"
#     fi
#   }
#
#   #######################################
#   # If declared, BT_TEAR_DOWN is called within the isolated test environment, just
#   # after invoking one of the test functions.
#   #
#   # BT_TEMP_DIR will be set to the temporary root directory created to execute
#   # a single test. This directory and all subdirectories will be deleted,
#   # automatically, after the test executes. BT_TEAR_DOWN should be used only
#   # if there are other resources to tear down, such as to kill a background
#   # process created by BT_SET_UP or one of the TEST_... functions.
#   #
#   # The current working directory will be set to the directory containing the
#   # BT_SET_UP bash function (from within the temporary root directory).
#   #
#   # Important: BT_TEAR_DOWN is only called after the test function returns or
#   # exits, *and* closes the stdout stream. If the test starts a background task
#   # witout redirecting stdout, and the test fails before completing or killing
#   # that background task, stdout will remain open and the test will hang. To
#   # avoid this, always redirect background task stdout. Among the typical
#   # alternatives are redirecting stdout to stderr ("some_program >&2"), a
#   # variable, a file, or /dev/null.
#   #######################################
#   # BT_TEAR_DOWN() {
#   # }
#
#   TEST_create_prefs_file {
#     BT_ASSERT_FUNCTION_EXISTS create_prefs_file
#     local -r content="config=DEBUG"
#     create_prefs_file .config "${content}"
#     BT_EXPECT_FILE_CONTAINS "${FUCHSIA_DIR}/out/default/.config" "${content}"
#   }
#
#   #######################################
#   # The last line of the test function should be the call to the
#   # bash_test_framework.sh declared function BT_RUN_TESTS, as follows:
#   #######################################
#   BT_RUN_TESTS "$@"
#---------------------------------------------------------------------------------------------------

declare -r -i MAX_ERROR_STATUS=255  # 0-255 is the range of values available for exit codes

# PRIVATE FUNCTIONS AND VARIABLES
#
# Framework-private functions begin with "btf::_" and private global variables
# begin with the prefix "_btf_" (or "_BTF_" for constants). The private
# functions and variables should not be called / used by test scripts.

# Constants
readonly _BTF_HOST_SCRIPT_NAME="$(basename $0)"
readonly _BTF_HOST_SCRIPT_DIR="$(cd "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
readonly _BTF_HOST_SCRIPT="${_BTF_HOST_SCRIPT_DIR}/${_BTF_HOST_SCRIPT_NAME}"
readonly _BTF_ASSERT_ERROR_COUNT_MESSAGE_PREFIX="Current error count is"
readonly _BTF_END_OF_TEST_MARKER=$'\nEOT'

# Default root assumes the bash_test_framework.sh script is a specific
# directory depth below the root of the project source tree. For example,
# under $FUCHSIA_DIR, the framework script is in "tools/devshell/tests".
# If necessary, override this assumption in the host test script by
# explicitly setting the variable BT_DEPS_ROOT to the root directory path.
readonly _BTF_FRAMEWORK_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
readonly _BTF_DEFAULT_ROOT_DIR="$(cd "${_BTF_FRAMEWORK_SCRIPT_DIR}/../../../.." >/dev/null 2>&1 && pwd)"

readonly _BTF_TEMP_DIR_MARKER=".btf"
readonly _BTF_FUNCTION_NAME_PREFIX="TEST_"

if [[ -t 2 ]]; then
  # stderr messages use ANSI terminal escape sequences only if output is tty
  readonly ESC=$'\033'
  readonly _ANSI_BRIGHT_RED="${ESC}[1;31m"
  readonly _ANSI_BRIGHT_GREEN="${ESC}[1;32m"
  readonly _ANSI_CLEAR="${ESC}[0m"
else
  readonly _ANSI_BRIGHT_RED=
  readonly _ANSI_BRIGHT_GREEN=
  readonly _ANSI_CLEAR=
fi

if [[ -t 1 ]]; then
  readonly _BTF_FAIL="${_ANSI_BRIGHT_RED}FAIL:${_ANSI_CLEAR}"
else
  # if stdout is not a tty, ensure failure messages don't use ANSI escape sequences
  readonly _BTF_FAIL="FAIL:"
fi

# For BT_EXPECT_{TRUE/FALSE}, when using 'eval' to execute a command, the ${FUNCNAME[@]}
# array should be interpreted with an additional offset, as if the command was
# actually executed from the context of the BT_ function that called eval.
declare _BTF_EVAL_OFFSET=0

# Ensure array types
declare -a BT_MKDIR_DEPS
declare -a BT_FILE_DEPS
declare -a BT_LINKED_DEPS
declare -a BT_MOCKED_TOOLS
declare -a BT_TEST_FUNCTIONS

#######################################
# Returns true (return status 0) if a function with the given name is declared.
# Arguments:
#   $1 - the function name to look for
# Returns:
#   0 if it exists, non-zero otherwise
#######################################
btf::function_exists() {
  local function_name="$1"
  declare -f "${function_name}" >/dev/null
}

# Define no-op versions of the following functions.
# These may be redefined in the host test script.
btf::function_exists BT_INIT_TEMP_DIR || BT_INIT_TEMP_DIR() { :; }
btf::function_exists BT_SET_UP || BT_SET_UP() { :; }
btf::function_exists BT_TEAR_DOWN || BT_TEAR_DOWN() { :; }

#######################################
# Prints a message to stderr.
# Arguments:
#   - optional flag "-n" to print the message without adding a newline
#   - message or format string
#   - remaining arg(s) - values to match the printf format string placeholders
# Outputs:
#   Writes the formatted message to stderr
#######################################
btf::stderr() {
  local newline=true
  if [[ "$1" == "-n" ]]; then
    newline=false; shift
  fi
  local format_string="$1"; shift
  >&2 printf "${format_string}" "$@"
  if ! [[ "${format_string}" =~ % ]]; then
    >&2 printf " %s" "$@"
  fi
  if ${newline}; then
    >&2 printf "\n"
  fi
}

#######################################
# Internal function called from public functions like "error", "failed", or
# "success"
# Arguments:
#   - optional flags that begin with "-"
#   - ANSI terminal escape sequence for the desired output style, if any
#   - A message prefix (may be an empty string, but the argument is still present)
#   - message or format string
#   - remaining arg(s) - values to match the printf format string placeholders
# Globals:
#   _ANSI_CLEAR
# Outputs:
#   Writes the formatted message to stderr
#######################################
btf::_print_with_style() {
  local flags=()
  while [[ "$1" == -* ]]; do
    flags+=( "$1" ); shift
  done
  local ansi_style="$1"; shift
  local prefix="$1"; shift
  local format_string="$1"; shift
  btf::stderr "${flags[@]}" "${ansi_style}${prefix}${format_string}${_ANSI_CLEAR}" "$@"
}

#######################################
# Prints "ERROR: " followed by a message. If printing to a terminal, error
# message style is applied (typically red)
# Arguments:
#   - optional flags that begin with "-"
#   - message or format string
#   - remaining arg(s) - values to match the printf format string placeholders
# Globals:
#   _ANSI_BRIGHT_RED
#   _BTF_DEFAULT_ROOT_DIR
# Outputs:
#   Writes the formatted message to stderr
#######################################
btf::error() {
  local flags=()
  while [[ "$1" == -* ]]; do
    flags+=( "$1" ); shift
  done
  local offset=0
  while [[ "${FUNCNAME[$((offset+1))]}" == "btf::"* ]]; do
    : $((offset++))
  done
  local source_file="${BASH_SOURCE[$((1+offset))]#$_BTF_DEFAULT_ROOT_DIR/}"
  local source_line=${BASH_LINENO[$((0+offset))]}
  btf::_print_with_style "${flags[@]}" "${_ANSI_BRIGHT_RED}" \
    "ERROR: ${source_file}:${source_line}: " "$@"
}

#######################################
# Invokes btf::error with the given message/format string and other arguments,
# then exits with a status 1.
# Arguments:
#   $1 - The status code to exit with
#   All remaining arguments are passed directly to btf::error. See btf::error
#   for additional detail.
# Outputs:
#   Writes the formatted message to stderr, possibly with ANSI terminal escapes.
#######################################
btf::abort() {
  local -i status=$1; shift
  if (( ${status} == 0 )); then
    btf::stderr "$@"
  else
    btf::error "$@"
  fi
  exit ${status}
}

#######################################
# Prints "FAILED: " followed by a message. If printing to a terminal, error
# message style is applied (typically red).
# Arguments:
#   - optional flags that begin with "-"
#   - message or format string
#   - remaining arg(s) - values to match the printf format string placeholders
# Globals:
#   _ANSI_BRIGHT_RED
# Outputs:
#   Writes the formatted message to stderr
#######################################
btf::failed() {
  local flags=()
  while [[ "$1" == -* ]]; do
    flags+=( "$1" ); shift
  done
  btf::_print_with_style "${flags[@]}" "${_ANSI_BRIGHT_RED}" "FAILED: " "$@"
}

#######################################
# Prints a success message. If printing to a terminal, success message style
# is applied (typically green)
# Arguments:
#   - optional flags that begin with "-"
#   - message or format string
#   - remaining arg(s) - values to match the printf format string placeholders
# Globals:
#   _ANSI_BRIGHT_GREEN
# Outputs:
#   Writes the formatted message to stderr
#######################################
btf::success() {
  local flags=()
  while [[ "$1" == -* ]]; do
    flags+=( "$1" ); shift
  done
  btf::_print_with_style "${flags[@]}" "${_ANSI_BRIGHT_GREEN}" "" "$@"
}

#######################################
# Writes a specific marker to stdout and calls exit with the given status code.
# When a test is invoked, stdout will be captured to a variable. The marker will
# be stripped from the stdout string before printing. If not found, the test
# did not complete a controlled test exit, and the user will be informed of
# the problem. For example, if a test calls a bash function, and that function
# calls "exit 0", the test will end. If there were no test failures to that
# point, the test will appear to have completed successfully. The marker ensures
# the test runs either to a failed "ASSERT" or to the end of the test function.
# Arguments:
#   $1 - The status code to exit with.
# Returns:
#   The current test error count.
#######################################
btf::_end_of_test() {
  local -i status=$1
  printf "%s" "${_BTF_END_OF_TEST_MARKER}"
  exit ${status}
}

#######################################
# Simply prints standard "existing" message to stdout and exits the script.
# Returns:
#   The current test error count.
#######################################
btf::_assert_failed() {
  if [[ ${_btf_test_pid} == ${BASHPID} ]]; then
    echo -n "Aborting test due to failed ASSERT"
  else
    echo -n "Exiting subshell"
  fi
  echo ". ${_BTF_ASSERT_ERROR_COUNT_MESSAGE_PREFIX} ${_btf_test_error_count}."
  # mark a controlled end of test, and return the error count from the subshell to main process
  btf::_end_of_test ${_btf_test_error_count}
}

#######################################
# Prints the failure message (using the custom message if provided,
# or the default message from the BT_EXPECT_... script), increments the
# error count, and returns the given status.
# Arguments:
#   $1 - The status code to return.
#   $2 - A default message for stdout, used if there are no more parameters.
#   $3 - (optional) A printf-style format string
#   Remaining args - to supply format parameters in the format string
# Returns:
#   The given status parameter value.
#######################################
btf::_fail() {
  local -i status=$1; shift
  local default_message="$1"; shift
  local format_string="$1"; shift

  if [[ "${format_string}" == "" ]]; then
    format_string="${default_message}"
  fi

  local func_offset=0
  local source_offset=0
  if [[ "${FUNCNAME[$((3+_BTF_EVAL_OFFSET))]}" != "btf::_run_isolated_test" ]]; then
    : $(( func_offset++ ))
  fi
  source_offset=$(( func_offset + _BTF_EVAL_OFFSET ))
  local called_function="${FUNCNAME[$((1+func_offset))]}"
  local test_file_loc="${BASH_SOURCE[$((2+source_offset))]#$BT_TEMP_DIR/}:${BASH_LINENO[$((1+source_offset))]}"
  printf "${_BTF_FAIL} ${test_file_loc}: (${called_function}) ${format_string}\n" "$@"
  : $(( _btf_test_error_count++ ))
  return ${status}
}

#######################################
# Outputs the absolute path to the given file, also resolving symbolic links.
# (Note: This attempts to use the 'realpath' binary, and falls back to Python
# otherwise)
# Arguments:
#   $1 - a relative file path. The full path does not have to exist, but
#   all left-most path components that do exist are evaluated, resolving
#   path symbols (such as ".", "..", "~") and replacing symbolic links
# Outputs:
#   Writes the absolute path to stdout
# Returns:
#   Exits the script with non-zero status if assertions fail
#######################################
btf::realpath() {
  local path="$1"
  [[ "${path}" != "" ]] \
      || btf::abort 1 "btf::realpath: input path cannot be blank"
  local rp
  local -i status
  if hash realpath >/dev/null 2>&1; then
    rp="$(realpath -m "$path" 2>/dev/null)"
    status=$?
  fi
  if [[ -z "${rp}" || ${status} -ne 0 ]]; then
    rp="$(python -c "import os; print(os.path.realpath('${path}'))")"
  fi
  [[ -n "${rp}" ]] \
      || btf::abort 1 "btf::realpath: result for '${path}' was unexpectedly blank, status=${status}"
  printf "%s" "${rp}"
  return ${status}
}

#######################################
# Evaluates the arguments (interprets the arguments as a command string, using the
# 'eval' built-in), generates a non-fatal failure if the return status is not zero (0).
# Arguments:
#   $@ - All arguments converted to a command string
# Returns:
#   The status code returned from the executed command
#######################################
BT_EXPECT() {
  local -i status
  : $(( _BTF_EVAL_OFFSET++ ))
  # Do not run in a subshell. The command may set variables in the current shell.
  eval "$@"
  status=$?
  : $(( _BTF_EVAL_OFFSET-- ))
  if (( $status == 0 )); then
    return 0
  fi
  btf::_fail ${status} "Exit code: ${status}; expected 0 status from: $*"
}

#######################################
# Evaluates the arguments (interprets the arguments as a command string, using the
# 'eval' built-in), generates a fatal failure if the return status is not zero (0).
# Arguments:
#   $@ - All arguments converted to a command string
# Returns:
#   0 if the command returned 0, exits the test script with 1 otherwise
#######################################
BT_ASSERT() {
  BT_EXPECT "$@" || btf::_assert_failed
}

#######################################
# Evaluates the arguments (interprets the arguments as a command string, using the
# 'eval' built-in), generates a non-fatal failure if the return status is zero (0).
# Arguments:
#   $@ - All arguments converted to a command string
# Returns:
#   0 if the command returned a non-zero status, 1 otherwise
#######################################
BT_EXPECT_FAIL() {
  local -i status
  : $(( _BTF_EVAL_OFFSET++ ))
  # Do not run in a subshell. The command may set variables in the current shell.
  # (Even though the command is expected to return an error status code, it may still
  # be expected to complete some changes and/or cause side effects. And for consistency
  # with BT_EXPECT(), both scripts should be run without a subshell.)
  eval "$@"
  status=$?
  : $(( _BTF_EVAL_OFFSET-- ))
  if (( $status != 0 )); then
    return 0
  fi
  btf::_fail 1 "Exit code: 0; expected non-zero status from: $*"
}

#######################################
# Evaluates the arguments (interprets the arguments as a command string, using the
# 'eval' built-in), generates a fatal failure if the return status is zero (0).
# Arguments:
#   $@ - All arguments converted to a command string
# Returns:
#   0 if the command returned a non-zero status, exits the test script with 1 otherwise
#######################################
BT_ASSERT_FAIL() {
  BT_EXPECT_FAIL "$@" || btf::_assert_failed
}

#######################################
# Generates a non-fatal failure if the first and second arguments
# are not equal.
# Arguments:
#   $1 - left argument
#   $2 - right argument
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the arguments are equal, or 1 otherwise
#######################################
BT_EXPECT_EQ() {
  local lhs="$1"; shift
  local rhs="$1"; shift
  if [[ "${lhs}" == "${rhs}" ]]; then
    return 0
  fi
  btf::_fail 1 "'${lhs}' != '${rhs}'" "$@"
}

#######################################
# Generates a fatal failure if the first and second arguments
# are not equal.
# Arguments:
#   $1 - left argument
#   $2 - right argument
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the arguments are equal, exits the test script with 1 otherwise
#######################################
BT_ASSERT_EQ() {
  BT_EXPECT_EQ "$@" || btf::_assert_failed
}

#######################################
# Generates a non-fatal failure if the first argument,
# assumed to be a return result, is non-zero.
# Arguments:
#   $1 - return result
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the given status is 0, or returns the given status otherwise
#######################################
BT_EXPECT_GOOD_STATUS() {
  local -i status="$1"; shift
  if [[ ${status} == 0 ]]; then
    return 0
  fi
  # return the given non-zero status (instead of 1, this time)
  btf::_fail ${status} "Returned status '${status}' is not a success" "$@"
}

#######################################
# Generates a fatal failure if the first argument,
# assumed to be a return result, is non-zero.
# Arguments:
#   $1 - return result
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the given status is 0, exits the test script with 1 otherwise
#######################################
BT_ASSERT_GOOD_STATUS() {
  BT_EXPECT_GOOD_STATUS "$@" || btf::_assert_failed
}

#######################################
# Generates a non-fatal failure if the first argument,
# assumed to be a return result, is zero (0), the success status.
# Arguments:
#   $1 - return result
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the given status is not 0, 1 otherwise
#######################################
BT_EXPECT_BAD_STATUS() {
  local -i status="$1"; shift
  if [[ ${status} != 0 ]]; then
    return 0
  fi
  # return 1 (*not* the given 0 status)
  btf::_fail 1 "Expected an error status, but 0 is not an error" "$@"
}

#######################################
# Generates a fatal failure if the first argument,
# assumed to be a return result, is zero (0), the success status.
# Arguments:
#   $1 - return result
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the given status is not 0, exits the test script with 1 otherwise
#######################################
BT_ASSERT_BAD_STATUS() {
  BT_EXPECT_BAD_STATUS "$@" || btf::_assert_failed
}

#######################################
# Generates a non-fatal failure if the first argument
# value is not an empty string.
# Arguments:
#   $1 - value to check
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the string is empty, non-zero otherwise
#######################################
BT_EXPECT_EMPTY() {
  local string="$1"; shift
  if [[ "${string}" == "" ]]; then
    return 0
  fi
  btf::_fail 1 "String '${string}' is not empty" "$@"
}

#######################################
# Generates a fatal failure if the first argument
# value is not an empty string.
# Arguments:
#   $1 - value to check
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the string is empty, exits the test script with 1 otherwise
#######################################
BT_ASSERT_EMPTY() {
  BT_EXPECT_EMPTY "$@" || btf::_assert_failed
}

#######################################
# Generates a non-fatal failure if the first argument
# value does not exist or is the empty string.
# Arguments:
#   $1 - value to check
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the string is not empty, non-zero otherwise
#######################################
BT_EXPECT_NOT_EMPTY() {
  local string="$1"; shift
  if [[ "${string}" != "" ]]; then
    return 0
  fi
  btf::_fail 1 "String is empty" "$@"
}

#######################################
# Generates a fatal failure if the first argument
# value does not exist or is the empty string.
# Arguments:
#   $1 - value to check
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the string is not empty, exits the test script with 1 otherwise
#######################################
BT_ASSERT_NOT_EMPTY() {
  BT_EXPECT_NOT_EMPTY "$@" || btf::_assert_failed
}

#######################################
# Generates a non-fatal failure if the file does not exist
# Arguments:
#   $1 - filename
#   $2 - expected file content
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the file exists, non-zero otherwise
#######################################
BT_EXPECT_FILE_EXISTS() {
  local filename="$1"; shift
  if [[ -e "${filename}" ]]; then
    return 0
  fi
  btf::_fail 1 "File '${filename}' not found" "$@"
}

#######################################
# Generates a fatal failure if the file does not exist
# Arguments:
#   $1 - filename
#   $2 - expected file content
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the file exists, exits the test script with 1 otherwise
#######################################
BT_ASSERT_FILE_EXISTS() {
  BT_EXPECT_FILE_EXISTS "$@" || btf::_assert_failed
}

#######################################
# Generates a non-fatal failure if the file exists
# Arguments:
#   $1 - filename
#   $2 - expected file content
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the file does not exist, non-zero otherwise
#######################################
BT_EXPECT_FILE_DOES_NOT_EXIST() {
  local filename="$1"; shift
  if [[ ! -e "${filename}" ]]; then
    return 0
  fi
  btf::_fail 1 "Existing file '${filename}' should not exist" "$@"
}

#######################################
# Generates a fatal failure if the file exists
# Arguments:
#   $1 - filename
#   $2 - expected file content
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the file does not exist, exits the test script with 1 otherwise
#######################################
BT_ASSERT_FILE_DOES_NOT_EXIST() {
  BT_EXPECT_FILE_DOES_NOT_EXIST "$@" || btf::_assert_failed
}

#######################################
# Generates a non-fatal failure if the file content
# does not match the given string
# Arguments:
#   $1 - filename
#   $2 - expected file content
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the substring was found, non-zero otherwise
#######################################
BT_EXPECT_FILE_CONTAINS() {
  local filename="$1"; shift
  local expected_content="$1"; shift
  if [[ -e "${filename}" ]]; then
    if [[ "$(cat "${filename}")" == "${expected_content}" ]]; then
      return 0
    fi
    btf::_fail 1 "File '${filename}' content does not match expected content:" "$@"
    echo "expected: '${expected_content}'"
    echo "  actual: '$(cat "${filename}")'"
  else
    btf::_fail 1 "File '${filename}' not found" "$@"
  fi
  return 1
}

#######################################
# Generates a fatal failure if the file content
# does not match the given string
# Arguments:
#   $1 - filename
#   $2 - expected file content
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the substring was found, exits the test script with 1 otherwise
#######################################
BT_ASSERT_FILE_CONTAINS() {
  BT_EXPECT_FILE_CONTAINS "$@" || btf::_assert_failed
}

#######################################
# Generates a non-fatal failure if the file
# does not contain the substring.
# Arguments:
#   $1 - filename
#   $2 - substring to look for in the file
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the substring was found, non-zero otherwise
#######################################
BT_EXPECT_FILE_CONTAINS_SUBSTRING() {
  local filename="$1"; shift
  local substring="$1"; shift
  if [[ -e "${filename}" ]]; then
    if grep -q "${substring}" "${filename}"; then
      return 0
    fi
    btf::_fail 1 "Substring '${substring}' not found in file '${filename}'" "$@"
    echo "actual file content: '$(cat "${filename}")'"
  else
    btf::_fail 1 "File '${filename}' not found" "$@"
  fi
  return 1
}

#######################################
# Generates a fatal failure if the file
# does not contain the substring.
# Arguments:
#   $1 - filename
#   $2 - substring to look for in test string
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the substring was found, exits the test script with 1 otherwise
#######################################
BT_ASSERT_FILE_CONTAINS_SUBSTRING() {
  BT_EXPECT_FILE_CONTAINS_SUBSTRING "$@" || btf::_assert_failed
}

#######################################
# Generates a non-fatal failure if no files in the directory
# contain the substring.
# Arguments:
#   $1 - directory name
#   $2 - substring to look for, in any file in the directory
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the substring was found, non-zero otherwise
#######################################
BT_EXPECT_DIRECTORY_CONTAINS_SUBSTRING() {
  local directory="$1"; shift
  local substring="$1"; shift
  if [[ -d "${directory}" ]]; then
    if grep -Rq "${substring}" "${directory}"; then
      return 0
    fi
    btf::_fail 1 "Substring '${substring}' not found in directory '${directory}'" "$@"
  else
    if [[ -e "${directory}" ]]; then
      btf::_fail 1 "File '${directory}' is not a directory" "$@"
    else
      btf::_fail 1 "Directory '${directory}' not found" "$@"
    fi
  fi
  return 1
}

#######################################
# Generates a fatal failure if no files in the directory
# contain the substring.
# Arguments:
#   $1 - directory name
#   $2 - substring to look for, in any file in the directory
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the substring was found, exits the test script with 1 otherwise
#######################################
BT_ASSERT_DIRECTORY_CONTAINS_SUBSTRING() {
  BT_EXPECT_DIRECTORY_CONTAINS_SUBSTRING "$@" || btf::_assert_failed
}

#######################################
# Generates a non-fatal failure if the string
# does not contain the substring.
# Arguments:
#   $1 - the full test string
#   $2 - substring to look for in test string
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the substring was found, non-zero otherwise
#######################################
BT_EXPECT_STRING_CONTAINS_SUBSTRING() {
  local string="$1"; shift
  local substring="$1"; shift
  if [[ "${string#*$substring}" != "${string}" ]]; then
    return 0
  fi
  btf::_fail 1 "Substring '${substring}' not found in string '${string}'" "$@"
  return 1
}

#######################################
# Generates a fatal failure if the string
# does not contain the substring.
# Arguments:
#   $1 - the full test string
#   $2 - substring to look for in test string
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the substring was found, exits the test script with 1 otherwise
#######################################
BT_ASSERT_STRING_CONTAINS_SUBSTRING() {
  BT_EXPECT_STRING_CONTAINS_SUBSTRING "$@" || btf::_assert_failed
}

#######################################
# Generates a non-fatal failure if the function
# identified by the first argument does not exist.
# BT_ASSERT_FUNCTION_EXISTS - Generates a fatal failure
# if the function identified by the first argument does not exist.
# Arguments:
#   $1 - function name
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the function exists, non-zero otherwise
#######################################
BT_EXPECT_FUNCTION_EXISTS() {
  local function="$1"; shift
  if btf::function_exists "${function}"; then
    return 0
  fi
  btf::_fail 1 "Function '${function}' not found" "$@"
}

#######################################
# Generates a fatal failure if the function
# identified by the first argument does not exist.
# Arguments:
#   $1 - function name
#   remaining arg(s) - custom error message (optional)
# Returns:
#   0 if the function exists, exits the test script with 1 otherwise
#######################################
BT_ASSERT_FUNCTION_EXISTS() {
  BT_EXPECT_FUNCTION_EXISTS "$@" || btf::_assert_failed
}

#######################################
# Returns true (status 0) if the given root directory is a
# temporary bash_test_framework test execution directory.
# Arguments:
#   $1 - directory path to check
# Globals:
#   _BTF_TEMP_DIR_MARKER - name of the marker file to look for in the directory
# Returns:
#   0 if the given directory path contains the marker file for the test execution dir,
#   non-zero otherwise
#######################################
btf::is_bt_temp_dir() {
  local root_dir="$(btf::realpath "$1")"; shift
  # As an additional safety measure, check the path name string length
  # in addition to checking for the marker file.
  local -r temp_dir_string_length=${#root_dir}
  (( ${temp_dir_string_length} > 10 )) && \
  [[ -f "${root_dir}/${_BTF_TEMP_DIR_MARKER}" ]]
}

#######################################
# Creates a temporary directory for isolated test scripts and files (copies and/or
# mock files), and adds a marker file to identify the directory as a bash_test_framework
# test execution directory.
# Outputs:
#   Writes the generated temporary directory name to stdout
# Globals:
#   _BTF_TEMP_DIR_MARKER - name of the marker file to add to the specified directory
# Returns:
#   1 if the directory could not be created, 0 otherwise
#######################################
btf::_make_temp_dir() {
  local temp_dir
  temp_dir="$(mktemp -d -t "tmp.fx-self-test.XXXXXX")" || btf::abort $? "Unable to create temporary test directory."

  touch "${temp_dir}/${_BTF_TEMP_DIR_MARKER}"

  printf "${temp_dir}"
}

#######################################
# Makes a mock executable (via hard link) from the bash test framework's mock.sh,
# creating intermediate directories if needed.
# Globals:
#   BT_TEMP_DIR - the root of the temporary test directory
# Returns:
#   1 if the mock executable could not be created, 0 otherwise
#######################################
btf::make_mock() {
  local mockpath="$1"
  local mockpath_realpath="$(btf::realpath "${mockpath}")"
  local -r bt_temp_dir_realpath="$(btf::realpath "${BT_TEMP_DIR}")"
  if [[ "${mockpath_realpath}" != "${bt_temp_dir_realpath}"/* ]]; then
    btf::error "mocked executable path '${mockpath_realpath}',
is outside the BT_TEMP_DIR root directory '${bt_temp_dir_realpath}'."
    return 1
  fi
  mkdir -p $(dirname "${mockpath_realpath}") || return $?
  ln -f "${BT_TEMP_DIR}/${_BTF_FRAMEWORK_SCRIPT_SUBDIR}/mock.sh" "${mockpath_realpath}"
}

btf::_simplify_mock_extension() {
  local extension="$1"
  local simplify=${extension//_/}
  simplify=${simplify//-/}
  simplify=${simplify/mock/}
  simplify=${simplify%s}
  printf "%s" "${simplify}"
}

btf::_sanity_check_mocks() {
  local linked_mock_script="${BT_TEMP_DIR}/${_BTF_FRAMEWORK_SCRIPT_SUBDIR}/mock.sh"
  local mocks=( $( find "${BT_TEMP_DIR}" -samefile "${linked_mock_script}" ) )
  local simplify
  local suggestion
  local valid_extensions="mock_stdout|mock_stderr|mock_status|mock_side_effects|mock_state"
  for mock in "${mocks[@]}"; do
    for file in $(ls "${mock}".* 2>/dev/null); do
      extension=${file##*.}
      if ! [[ "${extension}" =~ ${valid_extensions} ]]; then
        local suggestion=". Valid mock extensions are: ${valid_extensions//|/, }"
        extension_simplified="$(btf::_simplify_mock_extension ${extension})"
        local valid_extensions_array=( ${valid_extensions//|/ } )
        for valid_extension in "${valid_extensions_array[@]}"; do
          valid_simplified="$(btf::_simplify_mock_extension ${valid_extension})"
          if [[ "${extension_simplified}" == "${valid_simplified}" ]]; then
            suggestion=". Perhaps you meant to use the extension '${valid_extension}'"
          fi
        done
        echo "Unexpected file extension for mock executable '${file#$BT_TEMP_DIR}'${suggestion}."
        return 1
      fi
    done
  done
  return 0
}

#######################################
# Copies the testing framework, the test script, and its dependencies from the
# original source to the test execution temporary directory, and calls the
# BT_INIT_TEMP_DIR function from the new test execution directory, if given.
# Arguments:
#   $1 - relative path from the root to the host (test) script
# Globals:
#   _BTF_TEMP_DIR_MARKER (in)
#   _BTF_HOST_SCRIPT_NAME (in)
#   _BTF_FRAMEWORK_SCRIPT_SUBDIR (in)
#   BT_TEMP_DIR (propagated to subshell for optional BT_INIT_TEMP_DIR function)
#   BT_DEPS_DIR (original directory from which files will be copied to the temp dir)
# Outputs:
#   Error messages to stderr
# Returns:
#   1 on any failure, 0 if everything completed successfully
#######################################
btf::_init_temp_dir() {
  local host_script_subdir="$1"; shift
  # If the _BTF_TEMP_DIR_MARKER exists in the current working directory, the test
  # is running from a temp directory.
  if [[ -f "${BT_DEPS_ROOT}/${_BTF_TEMP_DIR_MARKER}" ]]; then
    btf::error "Attempted ${FUNCNAME[0]} from an existing temp directory."
    return 1
  fi

  local -r bt_deps_root_realpath="$(btf::realpath "${BT_DEPS_ROOT}")"
  local -r bt_temp_dir_realpath="$(btf::realpath "${BT_TEMP_DIR}")"

  # Copy the host script and the test framework.
  mkdir -p "${BT_TEMP_DIR}/${host_script_subdir}" || return $?
  cp "${BT_DEPS_ROOT}/${host_script_subdir}/${_BTF_HOST_SCRIPT_NAME}" \
     "${BT_TEMP_DIR}/${host_script_subdir}/${_BTF_HOST_SCRIPT_NAME}" || return $?

  mkdir -p "${BT_TEMP_DIR}/${_BTF_FRAMEWORK_SCRIPT_SUBDIR}" || return $?
  cp -r "${BT_DEPS_ROOT}/${_BTF_FRAMEWORK_SCRIPT_SUBDIR}/"* \
     "${BT_TEMP_DIR}/${_BTF_FRAMEWORK_SCRIPT_SUBDIR}/" || return $?
  # mock.sh will be hard linked to MOCKED_FILES. Prevent writing, but make it executable.
  chmod a-w,u+x "${BT_TEMP_DIR}/${_BTF_FRAMEWORK_SCRIPT_SUBDIR}/mock.sh" || return $?

  # Copy original files and/or directories (recursively) declared by the test,
  # creating intermediate directories as needed.
  for filepath in "${BT_FILE_DEPS[@]}"; do
    local filepath_realpath="$(btf::realpath "${BT_DEPS_ROOT}/${filepath}")"
    if [[ "${filepath_realpath}" != "${bt_deps_root_realpath}"/* ]]; then
      btf::error "BT_FILE_DEPS element '${filepath}' expands to '${filepath_realpath}',
which is outside the root directory '${bt_deps_root_realpath}'."
      return 1
    fi
    mkdir -p $(dirname "${BT_TEMP_DIR}/${filepath}") || return $?
    cp -r "${BT_DEPS_ROOT}/${filepath}" "${BT_TEMP_DIR}/${filepath}" || return $?
  done

  # Link original files/directories declared by the test,
  # creating intermediate directories as needed.
  for filepath in "${BT_LINKED_DEPS[@]}"; do
    local filepath_realpath="$(btf::realpath "${BT_DEPS_ROOT}/${filepath}")"
    if [[ "${filepath_realpath}" != "${bt_deps_root_realpath}"/* ]]; then
      btf::error "BT_LINKED_DEPS element '${filepath}' expands to '${filepath_realpath}',
which is outside the root directory '${bt_deps_root_realpath}'."
      return 1
    fi
    mkdir -p $(dirname "${BT_TEMP_DIR}/${filepath}") || return $?
    ln -s "${BT_DEPS_ROOT}/${filepath}" "${BT_TEMP_DIR}/${filepath}" || return $?
  done

  # Make mock executables as hardlinks to the BT_TEMP_DIR copy of the framework's mock.sh.
  for mockpath in "${BT_MOCKED_TOOLS[@]}"; do
    btf::make_mock "${BT_TEMP_DIR}/${mockpath}" || return $?
  done

  # Create additional directories declared by the test.
  for dirpath in "${BT_MKDIR_DEPS[@]}"; do
    local dirpath_realpath="$(btf::realpath "${BT_TEMP_DIR}/${dirpath}")"
    if [[ "${dirpath_realpath}" != "${bt_temp_dir_realpath}"/* ]]; then
      btf::error "BT_MKDIR_DEPS element '${dirpath}' expands to '${dirpath_realpath}',
which is outside the root directory '${bt_temp_dir_realpath}'."
      return 1
    fi
    mkdir -p "${BT_TEMP_DIR}/${dirpath}" || return $?
  done

  # Execute the BT_INIT_TEMP_DIR function
  (
    cd "${BT_TEMP_DIR}"
    BT_INIT_TEMP_DIR
  ) || return $?
}

#######################################
# Called after btf::_init_temp_dir, this function restarts the
# test script in a subshell, with a clean environment and with the
# current directory set to the BT_TEMP_ROOT directory. The function returns
# the status returned from the subshell.
# Arguments:
#   $1 - relative path from the root to the host (test) script
#   $2 - the name of the host (test) script
#   $3 - the name of the test function to run
# Globals:
#   USER (in)
#   HOME (in)
#   BT_TEMP_DIR (test execution dir path, propagated to subshell)
#   _BTF_FRAMEWORK_SCRIPT_SUBDIR (in)
#   _BTF_SUBSHELL_TEST_FUNCTION (propagated to subshell)
# Returns:
#   0 if the script was successfully executed and the test succeeded;
#   MAX_ERROR_STATUS if there was a test execution error;
#   otherwise, the count of test failures.
#######################################
btf::_launch_isolated_test_script() {
  local -i test_counter=$1; shift
  local host_script_subdir="$1"; shift
  local host_script_name="$1"; shift
  local test_function_name="$1"; shift

  local test_args=()
  if (( $# > 0 )); then
    test_args=( -- "$@" )
  fi

  # propagate certain bash flags if present
  shell_flags=()
  if [[ $- == *x* ]]; then
    shell_flags+=( -x )
  fi

  local host_script_dir="${BT_TEMP_DIR}/${host_script_subdir}"
  local host_script_path="${host_script_dir}/${host_script_name}"

  # Start a clean environment, cd to the BT_TEMP_DIR subdirectory containing
  # the test script, load the bash_test_framework.sh, then re-start the test
  # script for the specific test.
  local launch_script="$(cat << EOF
cd '${host_script_dir}'
source '${BT_TEMP_DIR}/${_BTF_FRAMEWORK_SCRIPT_SUBDIR}/bash_test_framework.sh' \
    || exit 1
source '${host_script_path}' $( (( ${#test_args[@]} > 0 )) && printf "'%s' " "${test_args[@]}") \
    || exit \$?
EOF
)"

  /usr/bin/env -i \
      USER="${USER}" \
      HOME="${HOME}" \
      BT_TEMP_DIR="${BT_TEMP_DIR}" \
      _BTF_FRAMEWORK_SCRIPT_SUBDIR="${_BTF_FRAMEWORK_SCRIPT_SUBDIR}" \
      _BTF_SUBSHELL_TEST_FUNCTION="${test_function_name}" \
      _BTF_SUBSHELL_TEST_NUMBER="${test_counter}" \
      bash "${shell_flags[@]}" \
      -c "${launch_script}" "${host_script_path}" \
      || return $?
}

#######################################
# Called after test execution, this function validates the given directory path
# is the temporary directory created for a test, then deletes the directory and all
# content, recursively.
# Arguments:
#   $1 - path to the BT_TEMP_DIR to clean up
# Globals:
#   _BTF_TEMP_DIR_MARKER (in)
# Returns:
#   0 if successful, otherwise, exits the script with a non-zero status.
#######################################
btf::_clean_up_temp_dir() {
  # Clean up. rm -rf is dangerous - make sure at least the temp dir string
  # is of reasonable length.
  if btf::is_bt_temp_dir "${BT_TEMP_DIR}"; then
    rm -rf "${BT_TEMP_DIR}"
  else
    btf::error "Invalid BT_TEMP_DIR dir path - aborting cleanup."
    btf::stderr "Given directory path was '${BT_TEMP_DIR}'."
    exit 1
  fi
}

#######################################
# Sets scoped local variables initialized by caller to values
# indicated by command line options, if provided.
# Arguments:
#   Command line arguments, if any
# Returns:
#   0 if successful, otherwise, exits the script with a non-zero status.
#######################################
btf::_get_options() {
  while (( $# > 0 )); do
    local opt="$1"; shift
    case "${opt}" in
      --test)
        (( $# > 0 )) || btf::abort 1 "Test option '--test TEST_name' is missing the test name"
        _BTF_TEST_NAME_FILTER="$1"; shift
        ;;
      --help)
        btf::stderr "
Test options include:
  --test <TEST_name>
        Run only the test matching the given name.
  -- args to add to BT_TEST_ARGS
Example:
  fx self-test my_script_test --test TEST_some_function -- --test specific --options here

Tests found in ${_BTF_HOST_SCRIPT}:
$(btf::_get_test_functions)
"
        exit 0
        ;;
      --)
        break
        ;;
      *)
        btf::abort 1 "Invalid test option: $opt. Try '--help' instead."
        ;;
    esac
  done
  # save any arguments after "--"
  BT_TEST_ARGS=( "$@" )
}

#######################################
# Called from a clean environment in a subshell, run the
# test function identified by name.
# Arguments:
#   $1 - Name of the test function to execute.
# Globals:
#   _ANSI_... constants (in)
#  _btf_test_error_count (in/out)
# Returns:
#   Failure count on test failures, MAX_ERROR_STATUS if there was a
#   test execution error (such as during test set up), or 0 if passed
#   (no errors or test failures)
#######################################
btf::_run_isolated_test() {
  local test_function_name="$1"

  # Safety checks
  btf::is_bt_temp_dir "${BT_TEMP_DIR}" \
      || btf::abort ${MAX_ERROR_STATUS} "BT_TEMP_DIR is not a valid temp dir path: ${BT_TEMP_DIR}"
  [[ "$(pwd)" == "${_BTF_HOST_SCRIPT_DIR}" ]] \
      || btf::abort ${MAX_ERROR_STATUS} "Current directory '$(pwd)' should be '${_BTF_HOST_SCRIPT_DIR}'"
  [[ "${_BTF_HOST_SCRIPT_DIR}" == "${BT_TEMP_DIR}"* ]] \
      || btf::abort ${MAX_ERROR_STATUS} "Test script dir '${_BTF_HOST_SCRIPT_DIR}' not in BT_TEMP_DIR='${BT_TEMP_DIR}'"

  BT_SET_UP \
      || btf::abort ${MAX_ERROR_STATUS} "BT_SET_UP function returned error status $?"

  if [[ $_BTF_SUBSHELL_TEST_NUMBER == 1 ]]; then
    local error_message=
    error_message="$(btf::_sanity_check_mocks)" \
        || btf::abort ${MAX_ERROR_STATUS} "${error_message} (error status $?)"
  fi

  # Call the test function.
  # Run the test in a subshell so any ASSERT will only exit the subshell
  local -i status
  local stdout
  stdout=$(
    local -i status=0
    local -i _btf_test_pid=${BASHPID}
    local -i _btf_test_error_count=0 # incremented by failed BT_EXPECT_... function calls
    ${test_function_name}
    status=$?
    # The test function is not required to return an error status, but if they do,
    # it should only be because a test failed.
    if [[ $status != 0 && $_btf_test_error_count == 0 ]]; then
      btf::abort ${MAX_ERROR_STATUS} \
          "Unexpected error status ${status} without incrementing _btf_test_error_count"
    fi
    # mark a controlled end of test, and return the error count from the subshell to main process
    btf::_end_of_test ${_btf_test_error_count}
  )
  status=$?
  local test_output="${stdout%$_BTF_END_OF_TEST_MARKER}"
  if (( ${#test_output} > 0 )); then
    printf "\n%s" "${test_output}"
  fi
  if [[ "${test_output}" == "${stdout}" ]]; then
    echo  # start error message on a new line
    btf::error "$(cat <<EOF
Test exited prematurely, with status ${status}.
If the test calls a function that invokes 'exit', use a subshell; for example:
  BT_EXPECT "( function_that_may_exit )"
EOF
)"
  fi
  if [[ ${status} == 0 ]]; then
    echo "[${_ANSI_BRIGHT_GREEN}PASSED${_ANSI_CLEAR}]"
  elif [[ ${status} == ${MAX_ERROR_STATUS} ]]; then
    echo "[${_ANSI_BRIGHT_RED}ERROR${_ANSI_CLEAR}]"
  else
    echo "[${_ANSI_BRIGHT_RED}FAILED${_ANSI_CLEAR}]"
  fi

  BT_TEAR_DOWN \
      || btf::abort ${MAX_ERROR_STATUS} "BT_TEAR_DOWN function returned error status $?"

  return ${status}
}

btf::_get_test_functions() {
  local bash_functions_declaration_order_sedprog="
    s/^ *\(${_BTF_FUNCTION_NAME_PREFIX}[-a-zA-Z0-9_]*\) *().*\$/\1/p;
    s/^ *function  *\(${_BTF_FUNCTION_NAME_PREFIX}[-a-zA-Z0-9_]*\).*\$/\1/p;
  "
  sed -ne "${bash_functions_declaration_order_sedprog}" "${_BTF_HOST_SCRIPT}"
}

#######################################
# Creates a temporary directory, copies scripts and resources
# to that directory, restarts the shell with a clean environment
# and sets the current directory to the root of the temporary
# directory, re-starts the test script, and executes the test
# script's functions prefixed with TEST_.
# Arguments:
#   Command line options (see btf::_get_options)
# Globals:
#   BT_DEPS_ROOT (in/out) directory path from which BT_FILE_DEPS,
#       BT_MKDIR_DEPS, and BT_MOCKED_TOOLS are relative paths. For Fuchsia
#       tests this is normally FUCHSIA_DIR, and can be derived by default
#       from the test script location.
#   _BTF_... - readonly values defined at the top of this script
# Returns:
#   error count on error (exits), 0 if passed (no errors or test failures)
#######################################
btf::_run_tests_in_isolation() {
  local -i test_counter=0
  local -i total_error_count=0
  local -i test_failure_count=0

  if [[ "${BT_DEPS_ROOT}" == "" ]]; then
    BT_DEPS_ROOT="${_BTF_DEFAULT_ROOT_DIR}"
  fi
  readonly BT_DEPS_ROOT
  local -r host_script_subdir="${_BTF_HOST_SCRIPT_DIR#$BT_DEPS_ROOT/}"
  local -r _BTF_FRAMEWORK_SCRIPT_SUBDIR="${_BTF_FRAMEWORK_SCRIPT_DIR#$BT_DEPS_ROOT/}"

  if [[ ${#BT_TEST_FUNCTIONS} == 0 ]]; then
    BT_TEST_FUNCTIONS=(
      $(btf::_get_test_functions)
    ) || return $?
  fi

  local has_filter=false
  local found_filtered_test=false
  if [[ "${_BTF_TEST_NAME_FILTER}" != "" ]]; then
    has_filter=true
  fi
  for next_test in "${BT_TEST_FUNCTIONS[@]}"; do
    if $has_filter; then
      if [[ "${next_test}" != "${_BTF_TEST_NAME_FILTER}" ]]; then
        continue
      else
        found_filtered_test=true
      fi
    fi
    : $(( test_counter++ ))
    BT_TEMP_DIR=$(btf::_make_temp_dir) \
        || return $?
    export BT_TEMP_DIR
    btf::_init_temp_dir "${host_script_subdir}" \
        || return $?

    # Launch the test in a subshell with clean environment
    echo -n "[${test_counter}] ${next_test}()  "
    local test_error_count=0
    btf::_launch_isolated_test_script \
        "${test_counter}" \
        "${host_script_subdir}" \
        "${_BTF_HOST_SCRIPT_NAME}" "${next_test}" "${BT_TEST_ARGS[@]}"
    test_error_count=$?

    if [[ test_error_count == ${MAX_ERROR_STATUS} ]]; then
      btf::abort ${test_error_count} "Fatal test execution error"
    fi

    # Keep temporary directory for debugging.
    if [[ ${test_error_count} == 0 ]]; then
      btf::_clean_up_temp_dir || return $?
    else
      echo "Preserving the temp directory: ${BT_TEMP_DIR}"
    fi

    if (( ${test_error_count} > 0 )); then
      : $(( test_failure_count++ ))
      : $(( total_error_count += test_error_count ))
    fi
  done

  if $has_filter && ! $found_filtered_test; then
    btf::failed "Test function '${_BTF_TEST_NAME_FILTER}' was not found"
    return 1
  fi

  if [[ ${test_failure_count} == 0 ]]; then
    if (( ${test_counter} == 1 )); then
      btf::success "1 test passed."
    else
      btf::success "All ${test_counter} tests passed."
    fi
    return 0
  fi

  local error_count_str
  if (( ${total_error_count} > 1 )); then
    error_count_str="(${total_error_count} errors)"
  else
    error_count_str="(1 error)"
  fi

  btf::failed "${test_failure_count} of ${test_counter} tests failed ${error_count_str}."

  # Do not return from the function.
  # Exit the script, with error count (0 if PASSED), so the
  # host script is not responsible for propagating the error.
  exit ${total_error_count}
}

#######################################
# On first invocation from host test script, _BTF_SUBSHELL_TEST_FUNCTION is not
# set, so the script calls btf::_run_tests_in_isolation to cycle through all
# declared TEST_... functions. If _BTF_SUBSHELL_TEST_FUNCTION is set, the test
# script has been re-entered, via a subshell with a clean environment, and the
# _BTF_SUBSHELL_TEST_FUNCTION contains the name of the next test function to
# execute.
# Arguments:
#   Command line options (forwarded to btf::_run_tests_in_isolation)
# Globals:
#   _BTF_SUBSHELL_TEST_FUNCTION (in), set only if script is re-entered
#   _btf_... collected function variables (scoped to function)
#   _BTF_... - readonly values defined at the top of this script
# Returns:
#   Failure count on test failures, MAX_ERROR_STATUS if there was a
#   test execution error (such as during test set up), or 0 if passed
#   (no errors or test failures)
#######################################
BT_RUN_TESTS() {
  # Get command line options
  local _BTF_TEST_NAME_FILTER=
  local BT_TEST_ARGS=()
  btf::_get_options "$@" || return $?

  local -i status=0
  if [[ "${_BTF_SUBSHELL_TEST_FUNCTION}" != "" ]]; then
    btf::_run_isolated_test "${_BTF_SUBSHELL_TEST_FUNCTION}" \
        || status=$?
  else
    btf::_run_tests_in_isolation "$@" \
        || status=$?
  fi

  # "exit" the script (do not "return"), with the error count, (error count is
  # 0 if test(s) PASSED), so the host script authors do not have to worry about
  # adding logic to propagating the error.
  exit ${status}
}
