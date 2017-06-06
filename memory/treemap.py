#!/usr/bin/env python
#
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Visualizes the output of Magenta's "ps --json" output.

Run "treemap.py --help" for a list of arguments.

For usage, see magenta/docs/memory.md
"""

import argparse
import json
import sys

class Node(object):
    def __init__(self):
        self.area = 0
        self.name = ''
        self.children = []

def lookup(koid):
    node = nodemap.get(koid)
    if node is None:
        node = Node()
        nodemap[koid] = node
    return node

def sum_area(node):
    node.area += sum(map(sum_area, node.children))
    return node.area

def format_size(bytes):
    units = "BkMGTPE"
    num_units = len(units)

    ui = 0
    r = 0
    whole = True

    while (bytes >= 10000 or (bytes != 0 and (bytes & 1023) == 0)):
        ui += 1
        if (bytes & 1023):
            whole = False
        r = bytes % 1024
        bytes /= 1024

    if (whole):
        return "{}{}".format(bytes, units[ui])

    round_up = ((r % 100) >= 50)
    r = (r / 100) + round_up
    if (r == 10):
        bytes += 1
        r = 0

    return "{}.{}{}".format(bytes, r, units[ui])

# See <https://github.com/evmar/webtreemap/blob/gh-pages/README.markdown#input-format>
# For a description of this data format.
def build_tree(node):
    return {
        'name': '{} ({})'.format(node.name, format_size(node.area)),
        'data': {
            '$area': node.area,
        },
        'children': map(build_tree, node.children)
    }

parser = argparse.ArgumentParser(
    'Output an HTML tree map of memory usuage')
parser.add_argument('--field',
                    help='Which memory field to display in the treemap',
                    choices=['private_bytes', 'shared_bytes', 'pss_bytes'],
                    default='pss_bytes')

args = parser.parse_args()

dataset = json.load(sys.stdin)
nodemap = {}
root_node = None

for record in dataset:
    record_type = record['type']
    # Only read processes and jobs.
    if record_type != 'p' and record_type != 'j':
        continue
    node = lookup(record['koid'])
    node.name = record['name']
    if record_type == 'p':
        node.area = record[args.field] or 0
    # The root node has a parent of zero.
    if record['parent'] == 0:
        root_node = node
    else:
        parent_node = lookup(record['parent'])
        parent_node.children.append(node)

if root_node is Node:
    print >> sys.stderr, 'error: Did not find root object'
    sys.exit(1)

sum_area(root_node)

print '''<!DOCTYPE html>
<title>Memory usage</title>
<script>
var kTree = %(json)s
</script>
<link rel='stylesheet' href='https://evmar.github.io/webtreemap/webtreemap.css'>
<style>
body {
  font-family: sans-serif;
  font-size: 0.8em;
  margin: 2ex 4ex;
}
h1 {
  font-weight: normal;
}
#map {
  width: 800px;
  height: 600px;
  position: relative;
  cursor: pointer;
  -webkit-user-select: none;
}
</style>

<h1>Memory usage (%(field)s)</h1>

<p>Click on a box to zoom in.  Click on the outermost box to zoom out.</p>

<div id='map'></div>

<script src='https://evmar.github.io/webtreemap/webtreemap.js'></script>
<script>
var map = document.getElementById('map');
appendTreemap(map, kTree);
</script>''' % {
    'json': json.dumps(build_tree(root_node)),
    'field': args.field
}
