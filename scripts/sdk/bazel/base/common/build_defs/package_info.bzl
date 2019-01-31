# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Some utilities to declare and aggregate package contents.
"""

# Identifies a component added to a package.
PackageComponentInfo = provider(
    fields = {
        "name": "name of the component",
        "manifest": "path to the component manifest file",
    },
)

# Represents a set of files to be added to a package.
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

# Aggregates the information provided by the above providers.
PackageAggregateInfo = provider(
    fields = {
        "components": "depset of (name, manifest) pairs",
        "mappings": "depset of (package dest, source) pairs",
    },
)

def get_aggregate_info(components, mappings, deps):
    transitive_components = []
    transitive_mappings = []
    for dep in deps:
        if PackageAggregateInfo not in dep:
            continue
        transitive_components.append(dep[PackageAggregateInfo].components)
        transitive_mappings.append(dep[PackageAggregateInfo].mappings)
    return PackageAggregateInfo(
        components = depset(components, transitive = transitive_components),
        mappings = depset(mappings, transitive = transitive_mappings),
    )

# Contains information about a built Fuchsia package.
PackageInfo = provider(
    fields = {
        "name": "name of the package",
        "archive": "archive file",
    },
)
