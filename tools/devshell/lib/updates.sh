# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

function check-for-amber-server {
  # Make sure it is running.
  if [[ -z "$(pgrep -f 'pm serve .*/amber-files')" ]]; then
    fx-error "It looks like serve-updates is not running."
    fx-error "You probably need to start \"fx serve\""
    return 1
  fi

  # Warn if it is using the wrong repository.
  if [[ -z "$(pgrep -f "pm serve .*${FUCHSIA_BUILD_DIR}/amber-files")" ]]; then
    fx-warn "WARNING: It looks like serve-updates is running in a different workspace."
    fx-warn "WARNING: You probably need to stop that one and start a new one here with \"fx serve\""
  fi
  return 0
}
