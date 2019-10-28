#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""The net module is a namespace for network handlers used by the graph server.

The net module provides:
 1. The ApiHandler which implements the JSON API for the service.
"""

from server.net.api_handler import ApiHandler
