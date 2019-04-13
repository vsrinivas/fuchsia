#!/bin/sh
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

echo >&2 '*** DEPRECATION WARNING ***'
echo >&2 "NOTE: The $0 script is obsolete and will be removed soon."
echo >&2 'For interactive use just run `fx build` or:'
echo >&2 '    ninja -C <outdir>.zircon'
echo >&2 '    ninja -C <outdir>'

exit 0
