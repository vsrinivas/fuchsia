# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Some utilities to declare and aggregate package contents.
"""

PackageLocalInfo = provider(
    fields = {
        "mappings": "list of (package dest, source) pairs",
    },
)

PackageAggregateInfo = provider(
    fields = {
        "contents": "depset of (package dest, source) pairs",
    },
)

def get_aggregate_info(mappings, deps):
    transitive_info = [dep[PackageAggregateInfo].contents for dep in deps]
    return PackageAggregateInfo(contents = depset(mappings,
                                                  transitive = transitive_info))
