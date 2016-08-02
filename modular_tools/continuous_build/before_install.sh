#!/bin/bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
set -ex

# Unencrypt files.
openssl aes-256-cbc \
  -K $encrypted_93f2d55815a3_key \
  -iv $encrypted_93f2d55815a3_iv \
  -in modular_tools/continuous_build/key.p12.enc \
  -out modular_tools/continuous_build/key.p12 \
  -d

openssl aes-256-cbc \
  -K $encrypted_93f2d55815a3_key \
  -iv $encrypted_93f2d55815a3_iv \
  -in modular_tools/continuous_build/boto.tmpl.enc \
  -out modular_tools/continuous_build/boto.tmpl \
  -d

# Install pyopenssl for deployment.
pip install --user pyopenssl
