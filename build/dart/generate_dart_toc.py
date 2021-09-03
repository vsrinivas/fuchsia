#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file
"""Generate _toc.yaml for a given dartdoc directory index.json.

This script takes in an index.json representing a dartdoc
directory and creates a _toc.yaml.
"""

import argparse
import collections
import json
import os
import sys
import yaml

# Dart types that can contain members.
ENCLOSING_TYPES = ["library", "class", "enum", "mixin", "extension"]
TYPE_HEADINGS = {
    "class": "Classes",
    "enum": "Enums",
    "extension": "Extensions",
    "mixin": "Mixins",
    "constant": "Constants",
    "constructor": "Constructors",
    "function": "Functions",
    "method": "Methods",
    "property": "Properties",
    "top-level constant": "Top-level Constants",
    "top-level property": "Top-level Properties",
    "typedef": "Typedefs",
}


def configure_yaml():
    # TODO: (https://fxbug.dev/83891) 
    # Reevaluate whether hack described below is required moving forward.
    # yaml.dump by default sorts keys alphabetically, but most of the time we
    # need a specific order. One workaround is by passing sort_keys=False, but
    # there is another problem: Python itself prior to 3.7 does not guarantee
    # dict key order. Therefore we use collections.OrderedDict, and we set up a
    # yaml representer to emit keys & values without the
    # "!!python/object/apply:collections.OrderedDict" tag.
    def represent_dictionary_order(dumper, data):
        return dumper.represent_mapping("tag:yaml.org,2002:map", data.items())

    yaml.add_representer(collections.OrderedDict, represent_dictionary_order)


def build_toc_item(title, path=None, sub_items=None):
    """Create an item in the TOC.

    Args:
      title (str) - Title for this item.
      path (str) - Path to the page for this item.
      sub_items (list) - Items to place in a collapsible subsection.
    """
    item = collections.OrderedDict()
    item["title"] = title
    if path:
        item["path"] = path
    if sub_items:
        item["section"] = sub_items
    return item


def build_toc_content(index_file):
    """Build a TOC hierarchy from a json index produced by Dartdoc.

     Args:
      index_file (str) - JSON file which represents dartdoc file.
    """

    with open(index_file) as f:
        data = json.load(f)

    libraries = treeify_index(data)

    toc_items = [build_toc_item("Overview", path="/reference/dart/index.md")]
    if libraries:
        if "packageName" in libraries[0]:
            # Organize by packageName, then alphabetically
            libraries.sort(key=lambda lib: (lib["packageName"], lib["name"]))

            current_package = None
            for lib in libraries:
                # Insert a heading when we encounter a new packageName.
                if current_package != lib["packageName"]:
                    current_package = lib["packageName"]
                    toc_items.append({"heading": current_package})

                toc_items.append(element_to_toc_item(lib))

        else:
            # Older versions of Dartdoc do not provide packageName in the index.
            # TODO: (https://fxbug.dev/83893)
            # Determine if this else branch is still needed for older dartdoc versions.
            toc_items.append({"heading": "Libraries"})
            toc_items.extend(element_to_toc_item(lib) for lib in libraries)

    return {"toc": toc_items}


def treeify_index(data):
    """Convert the index data to a tree and return a list of libraries.

    Index data is a flat list where each element identifies its enclosing
    element, if applicable. Libraries are not enclosed by anything and are
    effectively the root(s) of the tree. Other elements are added to a list
    named 'members' in their enclosing element. All other data is unchanged.

    Args:
      data (list) - list of libraries with possible nested elements.
    """
    assert type(data) is list

    libraries = []
    lookup = {}
    for element in data:
        if element["type"] == "library":
            libraries.append(element)

        if element["type"] in ENCLOSING_TYPES:
            # In some cases qualifiedName is not unique so we use packageName too.
            lookup_key = "%s-%s" % (
                element["qualifiedName"], element["packageName"])
            lookup[lookup_key] = element
            element["members"] = []

        if "enclosedBy" in element:
            # enclosedBy['name'] is not specific enough for lookup (e.g. two
            # libraries could both define a class 'Foo', and if we encounter
            # something enclosed by 'Foo', we wouldn't know which is the correct
            # 'Foo'). Instead we use this element's qualifiedName up to but not
            # including the last dot-for uniqueness we also use packageName too.
            qual_name = element["qualifiedName"]
            lookup_key = "%s-%s" % (
                qual_name[:qual_name.rfind(".")], element["packageName"])
            enclosingElement = lookup[lookup_key]
            enclosingElement["members"].append(element)

    return libraries


def element_to_toc_item(element):
    """Convert an element to a TOC item, recursively converting children.

    Args:
      element (dict) - tree element represented as a dict.
    """

    sub_items = []
    if "members" in element:
        # Group members by type, then alphabetically.
        element["members"].sort(
            key=lambda member: (member["type"], member["name"]))

        current_type = None
        for member in element["members"]:
            # Insert a heading when we encounter a new type grouping.
            if current_type != member["type"]:
                current_type = member["type"]
                sub_items.append({"heading": TYPE_HEADINGS[current_type]})

            sub_items.append(element_to_toc_item(member))
    return build_toc_item(
        title=element["name"],
        path="/reference/dart/%s" % element["href"],
        sub_items=sub_items,
    )


def no_args_main(index_file, outfile):
    """Modified main function which generates a toc.yaml.

    Args:
      index_file (str) - index.json file representing generated docs
      outfile (str) - output toc.yaml file
    """
    if not os.path.isfile(index_file):
        raise RuntimeError("%s does not exist or is not a file" % index_file)

    configure_yaml()
    content = build_toc_content(index_file)
    if not content:
        raise RuntimeError("Unable to generate toc content. Build failed.")
    with open(outfile, "w") as f:
        yaml.dump(content, f, default_flow_style=False)
