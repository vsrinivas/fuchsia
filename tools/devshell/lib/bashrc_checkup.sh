# No #!/bin/bash - See "usage"
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

### check the health of a user's interactive bash environment (from "fx doctor")

## usage:
##   "${SHELL}" [-flags] "${FUCHSIA_DIR}/tools/devshell/lib/bashrc_checkup.sh" || status=$?
##   (Valid only for bash ${SHELL} since this script is bash.)

# Detect potential problems for Fuchsia development from settings specific
# to the user's interactive shell environment. Potential customizations can
# include bash version and settings introduced in the user's ~/.bashrc file
# such as bash functions, aliases, and non-exported variables such as
# "${CDPATH}" and "${PATH}" that can impact how bash executes some commands
# from the command line.
#

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"/vars.sh || exit $?
source "${FUCHSIA_DIR}/tools/devshell/lib/style.sh" || exit $?
source "${FUCHSIA_DIR}/tools/devshell/lib/common_term_styles.sh" || exit $?

fx-config-read || exit $?

# For bash users, this script also attempts to load your preferred
# bash interpreter (if different from the default, such as Homebrew
# bash on Mac), and load your ~/.bashrc settings, as would happen
# in an interactive shell. This allows doctor to check for
# potential issues with settings that don't normally propagate to
# bash scripts (unless executed with "source"), such as bash
# functions, aliases, and unexported variables.

# For bash users, load settings that would exist in the user's
# interactive bash shells as per
# [The GNU Bash Reference Manual, for Bash, Version 4.4](https://www.gnu.org/software/bash/manual/html_node/Bash-Startup-Files.html)
if [ -f ~/.bashrc ]; then
  source ~/.bashrc
fi

check_cd() {
  # Returns an error status if the current definition of "cd"
  # writes anything to the stdout stream, which would break common bash
  # script lines similar to the following:
  #
  #   SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

  local cd_output=$(
    CDPATH=""
    cd "${FUCHSIA_DIR}" 2>/dev/null
    cd scripts 2>/dev/null
  )

  local cdpath_output=$(
    if [ -z "${cd_output}" ] && [ "${CDPATH}" != "" ]; then
      CDPATH="${FUCHSIA_DIR}"
      cd scripts 2>/dev/null
    fi
  )

  if [ -z "${cdpath_output}" ] && [ -z "${cd_output}" ]; then
    return 0  # The check passed!
  fi

  # The check failed. Print recommendations based on what we found.

  local status=1

  warn 'Your implementation of the "cd" command writes to stdout.'

  details << EOF
Many common developer scripts and tools use "cd" to find relative
file paths and will fail in unpredictable ways.
EOF

  if [ ! -z "${cdpath_output}" ]; then
    details << EOF

The "cd" command writes to stdout based on your CDPATH environment variable.

You can remove or unset CDPATH in your shell initialization script, or
define a cd wrapper function.
EOF
  fi

  details << EOF

If you have not redefined "cd", and the builtin "cd" is writing to stdout,
define a wrapper function and redirect the output to /dev/null or stderr.

EOF
  code << EOF
cd() {
  builtin cd "\$@"
}
EOF
  details << EOF

If you already redefine "cd" during shell initialization, find the alias,
function, or script, and either remove it, or redirect the output to stderr
by appending "", as in this example:

EOF
  code << EOF
cd() {
  builtin cd "\$@" >/dev/null
  update_terminal_cwd
}
EOF
  details << EOF

(Note, in this example, "update_terminal_cwd" is a common MacOS function
to call when changing directories. Other common "cd" overrides may invoke
"pwd", "print", or other commands.)

EOF

  return ${status}
}

main() {
  local status=0

  check_cd || status=$?

  return ${status}
}

main "$@" || exit $?
