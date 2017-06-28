#!/usr/bin/env python
#
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Visualizes the output of Magenta's "memgraph" tool.

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
    units = 'BkMGTPE'
    num_units = len(units)

    ui = 0
    r = 0
    whole = True

    while bytes >= 10000 or (bytes != 0 and (bytes & 1023) == 0):
        ui += 1
        if bytes & 1023:
            whole = False
        r = bytes % 1024
        bytes /= 1024

    if whole:
        return '{}{}'.format(bytes, units[ui])

    round_up = (r % 100) >= 50
    r = (r / 100) + round_up
    if r == 10:
        bytes += 1
        r = 0

    return '{}.{}{}'.format(bytes, r, units[ui])


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


def dump_html(node, depth=0, parent_area=None, total_area=None):
    """Returns an HTML representation of the tree.

    Args:
      node: The root of the tree to dump
      depth: The depth of the node. Use 0 for the root node.
      parent_area: The size of the parent node; used to show fractions.
          Use None for the root node.
      total_area: The total size of the tree; used to show fractions.
          Use None for the root node.
    Returns:
      A sequence of HTML lines, joinable by whitespace.
    """
    lines = []

    if not depth:
        # We're the root node. Dump the headers.
        lines.extend([
            '<style>',
            'table#tree tr:nth-child(even) {',
            '    background-color: #eee;',
            '}',
            'table#tree tr:nth-child(odd) {',
            '    background-color: #fff;',
            '}',
            'table#tree td {',
            '    text-align: right;',
            '    padding-left: 1em;',
            '    padding-right: 1em;',
            '    font-family:Consolas,Monaco,Lucida Console,Liberation Mono,'
            '        DejaVu Sans Mono,Bitstream Vera Sans Mono,Courier New,'
            '        monospace;',
            '}',
            'table#tree td.name {',
            '    text-align: left;',
            '}',
            '</style>',
            '<table id="tree">',
            '<tr>',
            '<th>Name</th>',
            '<th>Size<br/>(bytes/1024^n)</th>',
            '<th>Size (bytes)</th>',
            '<th>Fraction of parent</th>',
            '<th>Fraction of total</th>',
            '</tr>',
        ])

    lines.extend([
        '<tr>',
        # Indent the names based on depth.
        '<td class="name"><span style="color:#bbb">{indent}</span>'
            '{name}</td>'.format(
                    indent=('|' + '&nbsp;' * 2) * depth,
                    name=node.name),
        '<td>{fsize}</td>'.format(fsize=format_size(node.area)),
        '<td>{size}</td>'.format(size=node.area),
    ])

    if depth:
        # We're not the root node.
        pfrac = node.area / float(parent_area) if parent_area else 0
        tfrac = node.area / float(total_area) if total_area else 0
        for frac in (pfrac, tfrac):
            lines.extend([
                '<td>{pct:.3f}%&nbsp;<progress value="{frac}"></progress></td>'
                .format(pct=frac * 100, frac=frac)
            ])
    else:
        lines.append('<td></td>' * 2)
    lines.append('</tr>')

    if total_area is None:
        total_area = node.area

    # Append children by size, largest to smallest.
    def dump_child(child):
        return dump_html(child, depth=depth+1,
                parent_area=node.area, total_area=total_area)

    children = sorted(node.children, reverse=True, key=lambda n: n.area)
    for line in [dump_child(c) for c in children]:
        lines.extend(line)

    if not depth:
        lines.append('</table>')

    return lines


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
free_node = None

for record in dataset:
    record_type = record['type']
    # Only read certain types.
    if record_type not in ('kernel', 'j', 'p'):
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
    if not record['parent']:
        # The root node has an empty parent.
        if root_node:
            print >> sys.stderr, 'error: Found multiple root objects'
            sys.exit(1)
        root_node = node
    elif record['id'] == 'kernel/free':
        # Don't include the free space directly.
        free_node = node
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
vmo_node = lookup('kernel/vmo')
vmo_node.name = 'VMOs/processes'

# Create a fake entry to cover the portion of kernel/vmo that isn't
# covered by the job tree.
node = lookup('kernel/vmo/unknown')
node.name = 'unknown (kernel & unmapped)'
node.area = vmo_node.area - root_job.area
vmo_node.children.append(node)

# Render the tree.
tree_json = json.dumps(build_tree(root_node), indent=2)

# Include the free node in the HTML version.
if free_node:
    lookup('kernel/physmem').children.append(free_node)
tree_html = '\n'.join(dump_html(root_node))

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
</script>

<hr>
%(html)s

''' % {
    'json': tree_json,
    'field': args.field,
    'html': tree_html
}
