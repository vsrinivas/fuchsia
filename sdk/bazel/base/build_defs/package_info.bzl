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

# Identical to PackageLocalInfo, but a different type is needed when that
# information if generated from an aspect so that it does not collide with any
# existing PackageLocalInfo returned provider.
PackageGeneratedInfo = provider(
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
    transitive_info = []
    for dep in deps:
        if PackageAggregateInfo not in dep:
            continue
        transitive_info.append(dep[PackageAggregateInfo].contents)
    return PackageAggregateInfo(contents = depset(mappings,
                                                  transitive = transitive_info))
