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
root_job = None

for record in dataset:
    record_type = record['type']
    # Only read certain types.
    if record_type not in ('kernel', 'j', 'p'):
        continue
    if record['id'] == 'kernel/free':
        # Don't explicitly show the free space.
        continue
    node = lookup(record['id'])
    node.name = record['name']
    if record_type == 'kernel':
        node.area = record.get('size_bytes', 0)
    elif record_type == 'j':
        if record['parent'].startswith('kernel/'):
            if root_job:
                print >> sys.stderr, 'error: Found multiple root jobs'
                sys.exit(1)
            root_job = node
    elif record_type == 'p':
        node.area = record.get(args.field, 0)
    # The root node has an empty parent.
    if record['parent'] == '':
        if root_node:
            print >> sys.stderr, 'error: Found multiple root objects'
            sys.exit(1)
        root_node = node
    else:
        parent_node = lookup(record['parent'])
        parent_node.children.append(node)

if not root_node:
    print >> sys.stderr, 'error: Did not find root object'
    sys.exit(1)

if not root_job:
    print >> sys.stderr, 'error: Did not find root job'
    sys.exit(1)

# A better name for physmem.
lookup('kernel/physmem').name = 'All physical memory'

# Sum up the job tree. Don't touch kernel entries, which already have
# the correct sizes.
sum_area(root_job)

# The root job is usually named "root";
# make it more clear that it's a job.
root_job.name = 'root job'

# Give users a hint that processes live in the VMO entry.
vmo_node = lookup('kernel/vmo');
vmo_node.name = 'VMOs/processes'

# Create a fake entry to cover the portion of kernel/vmo that isn't
# covered by the job tree.
node = lookup('kernel/vmo/unknown')
node.name = 'unknown (kernel & unmapped)'
node.area = vmo_node.area - root_job.area
vmo_node.children.append(node)

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
    'json': json.dumps(build_tree(root_node), indent=2),
    'field': args.field
}
