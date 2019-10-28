#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""The server module defines an API server that provides the component graph.

The server module is responsible for analyzing data provided by the Fuchsia
Package Manager and returning that processed data through JSON APIs. This layer
is designed to help analyze the structure of Fuchsia and make it easier to see
the system as a whole.
"""

import server.fpm
import server.graph
import server.net
import server.query
import server.util
