#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script is required by fx emu to bring up the network
# interface on Mac OSX. Do not remove or edit this script
# without testing it against fx emu.

sudo ifconfig tap0 inet6 fc00::/7 up
