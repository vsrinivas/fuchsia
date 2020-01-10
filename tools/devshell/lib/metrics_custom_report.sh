# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Report events to the metrics collector from non-shell subcommands.
# How to use it:
#   - if the caller subcommand is Bash, don't use this, just source
#     lib/metrics.sh and call track-subcommand-custom-event directly
#   - if the caller subcommand is in any other language, fork a process
#     and execute:
#       bash ${FUCHSIA_DIR}/tools/devshell/lib/metrics_custom_report.sh SUBCOMMAND ACTION [LABEL]
#
#  Custom events will be reported to Google Analytics, if metrics are enabled, as:
#      event category="fx_custom_SUBCOMMAND"
#      event_action="ACTION"
#      event label="LABEL"
#
# This command respects the user's opt-in/out set by fx metrics.
#
# WARNING: This is not supposed to be directly executed by users.
set -e

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"/vars.sh || exit $?
fx-config-read
declare -r metrics_sh="${FUCHSIA_DIR}/tools/devshell/lib/metrics.sh"
source "${metrics_sh}" || exit $?

if [[ $# -lt 2 ]]; then
  exit 1
fi

track-subcommand-custom-event "$@"
