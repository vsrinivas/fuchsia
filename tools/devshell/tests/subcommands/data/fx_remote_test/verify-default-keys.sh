#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Always completes verification successfully. Returning true here simulates any
# case where default SSH credentials are unambiguously available, such as after
# a successful jiri clone or jiri update.
function verify_default_keys {
  true
}
