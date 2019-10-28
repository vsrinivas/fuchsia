#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""The query module implements the core fuchsia query language.

The query modules acts as a coordination layer between the graph and the fpm
modules. It is responsible for processing data retrieved from fpm and feeding
that processed data into the graph to generate a well formed graph.
"""

from server.query.query_handler import QueryHandler
