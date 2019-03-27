# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

### add-on functions for common styling of text messages to the terminal

## "source style.sh" before sourcing this script.
## Functions include:
##
## * info
## * warn
## * error
## * link
## * code
## * details

## usage examples:
##
## # First import style.sh
##
## source "$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"/lib/vars.sh || exit $?
## source "${FUCHSIA_DIR}/tools/devshell/lib/style.sh" || exit $?
## source "${FUCHSIA_DIR}/tools/devshell/lib/common_term_styles.sh" || exit $?
##
##  warn 'The warning message.'
##
##  details << EOF
##A multi-line message with bash ${variable} expansion.
##Escape dollars with backslash \$
##See $(link 'https://some/hyper/link') to insert a link.
##EOF
##
## Visual tests (and demonstration of capabilities) can be run from:
##   //scripts/tests/common_term_styles-test-visually

info() {
  style::info --stdout "
INFO: $@
"
}

warn() {
  style::warning --stdout "
WARNING: $@
"
}

error() {
  style::error --stdout "
ERROR: $@
"
}

details() {
  style::cat --indent 2
}

code() {
  style::cat --bold --magenta --indent 4
}

link() {
  style::link "$@"
}
