#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Fuchsia Package Manager module provides a python binding to the network api.

The FPM module is responsible for providing a simple interface to the JSON API
provided by the Fuchsia Package Management System.
"""

from server.fpm.package_manager import PackageManager
