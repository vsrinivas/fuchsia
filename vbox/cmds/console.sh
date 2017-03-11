#!/usr/bin/env bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# console connects the VM serial console to the current terminal output. You can
# think of this as "telneting to the VM console"
oldtty=$(stty -g)
trap "stty $oldtty" EXIT
echo "Use CTRL+Q to exit the serial console"
socat unix-connect:${FUCHSIA_OUT_DIR}/vbox/${FUCHSIA_VBOX_NAME}.sock stdio,raw,echo=0,icanon=0,escape=0x11
