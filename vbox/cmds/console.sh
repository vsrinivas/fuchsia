#!/usr/bin/env bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# console connects the VM serial console to the current terminal output. You can
# think of this as "telneting to the VM console"
oldtty=$(stty -g)
trap "stty $oldtty" EXIT
echo "Connecting to ${FUCHSIA_OUT_DIR}/vbox/${FUCHSIA_VBOX_NAME}.sock"
echo "Use CTRL+Q to exit the serial console"
socat stdio,rawer,escape=0x11 unix-connect:${FUCHSIA_OUT_DIR}/vbox/${FUCHSIA_VBOX_NAME}.sock