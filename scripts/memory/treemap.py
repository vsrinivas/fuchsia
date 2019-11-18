#!/usr/bin/env python2.7
#
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Visualizes the output of Zircon's "memgraph" tool.

For usage, see
https://fuchsia.dev/fuchsia-src/development/memory/memory#visualize_memory_usage
"""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import cgi
import collections
import json
import sys
import os.path
import textwrap

FUCHSIA_DIR = os.path.abspath(os.path.join(__file__, os.pardir, os.pardir, os.pardir))

# Magic value for nodes with empty names.
UNNAMED_NAME = '<unnamed>'


class Node(object):
    """A generic node in the kernel/job/process/memory tree."""

    def __init__(self):
        self.type = ''
        self.koid = 0
        self.name = ''
        self.area = 0
        self.children = []

    def html_label(self):
        """Returns a safe HTML string that identifies this Node."""
        tag = ''
        if self.type:
            tag = ('<span class="treemap-node-type '
                   'treemap-node-type-{}">{}</span> ').format(
                           cgi.escape(self.type[0]),
                           cgi.escape(self.type[0].upper()))
        if self.name in ('', UNNAMED_NAME):
            name = '<i>UNNAMED</i> [koid {}]'.format(self.koid)
        else:
            name = cgi.escape(self.name)
        return tag + name


# Mapping of unique ID strings to Node objects.
ids_to_nodes = {}


def lookup(node_id):
    """Returns or creates the Node associated with an ID string.

    Args:
        node_id: ID string to look up
    Returns:
        A Node object
    """
    node = ids_to_nodes.get(node_id)
    if node is None:
        node = Node()
        ids_to_nodes[node_id] = node
    return node


def sum_area(node):
    """Recursively calculates the Node.area values of a subtree.

    Args:
        node: The Node at the root of the tree to walk
    Returns:
        The final area of |node|
    """
    # Area should either be set explicitly or calculated from the children.
    if node.children and (node.area != 0):
        raise AssertionError(
                'Node {} has {} children and non-zero area {}'.format(
                        node.name, len(node.children), node.area))
    node.area += sum(map(sum_area, node.children))
    return node.area


def format_size(nbytes):
    """Formats a size as a human-readable string like "123.4k".

    Units are in powers of 1024, so "k" is technically "kiB", etc.
    Values smaller than "k" have the suffix "B".

    Exact multiples of a unit are displayed without a decimal;
    e.g., "17k" means the value is exactly 17 * 1024.

    Otherwise, a decimal is present; e.g., "17.0k" means the value
    is (17 * 1024) +/- epsilon.

    Args:
        nbytes: The value to format
    Returns:
        The formatted string
    """
    units = 'BkMGTPE'
    ui = 0
    r = 0
    whole = True

    while nbytes >= 10000 or (nbytes != 0 and (nbytes & 1023) == 0):
        ui += 1
        if nbytes & 1023:
            whole = False
        r = nbytes % 1024
        nbytes //= 1024

    if whole:
        return '{}{}'.format(nbytes, units[ui])

    round_up = (r % 100) >= 50
    r = (r // 100) + round_up
    if r == 10:
        nbytes += 1
        r = 0

    return '{}.{}{}'.format(nbytes, r, units[ui])


# Enum for tracking VMO reference types.
VIA_HANDLE = 1
VIA_MAPPING = 2


def populate_process(process_node, process_record, hide_aggregated=True):
    """Adds the process's child nodes.

    Args:
        process_node: A process's Node
        process_record: The same process's input record
        hide_aggregated: If true, do not create Nodes for individual VMOs
                that have been aggregated by name into a single Node
    """
    # If there aren't any VMOs, use the sizes in the record.
    if not process_record.get('vmo_refs', []):
        # Get the breakdown.
        priv = process_record.get('private_bytes', 0)
        pss = process_record.get('pss_bytes', 0)
        shared = max(0, pss - priv)  # Kernel calls this "scaled shared"

        pid = process_record['id']
        if priv:
            node = lookup(pid + '/priv')
            node.name = 'Private'
            node.area = priv
            process_node.children.append(node)
        if shared:
            node = lookup(pid + '/shared')
            node.name = 'Proportional shared'
            node.area = shared
            process_node.children.append(node)
        # The process's area will be set to the sum of the children.
        return
    # Otherwise, this entry has VMOs.

    # Build the set of reference types from this process to its VMOs.
    koid_to_ref_types = collections.defaultdict(set)
    for vmo_ref in process_record.get('vmo_refs', []):
        ref_types = koid_to_ref_types[vmo_ref['vmo_koid']]
        if 'HANDLE' in vmo_ref['via']:
            ref_types.update([VIA_HANDLE])
        if 'MAPPING' in vmo_ref['via']:
            ref_types.update([VIA_MAPPING])

    # De-dup the set of VMOs known to the process, and group them by name. Each
    # of these entries are equivalent, though some values may be different (like
    # committed_bytes) because they were snapshotted at different times.
    name_to_vmo = collections.defaultdict(list)
    koid_to_vmo = dict()
    id_prefix = '{}/vmo'.format(process_record['id'])
    for vmo in process_record.get('vmos', []):
        # Although multiple processes may point to the same VMO, we're building
        # a tree and thus need to create unique IDs for VMOs under this process.
        vmo_koid = vmo['koid']
        vmo_id = '{}/{}'.format(id_prefix, vmo_koid)
        vmo_node = lookup(vmo_id)
        if vmo_node.name:
            # This is a duplicate of a VMO we've already seen.
            continue

        vmo_node.type = 'vmo'
        vmo_node.koid = vmo_koid
        vmo_node.name = vmo['name'] if vmo['name'] else UNNAMED_NAME
        name_to_vmo[vmo_node.name].append(vmo_node)
        koid_to_vmo[vmo_koid] = vmo_node

        # Figure out a size for the VMO.
        ref_types = koid_to_ref_types[vmo_koid]
        if VIA_MAPPING in ref_types:
            # The VMO is already accounted for in the process's pss_bytes value.
            # TODO(dbort): To make the VMO areas exactly line up with pss_bytes,
            # we'd need sub-VMO mapping information like what 'vmaps' provides:
            # this process may only map a subset of the VMO's committed pages,
            # but we're counting all of them. This isn't necessarily wrong,
            # just different.
            vmo_node.area = int(
                    float(vmo['committed_bytes']) / vmo['share_count'])
            # NB: This counts as private memory if share_count is 1.
        else:
            # The process only has a handle to this VMO but does not map it: the
            # process's pss_bytes value does not account for this VMO.
            assert ref_types == set([VIA_HANDLE])
            # Treat our handle reference as an increment to the VMO's
            # share_count. This may over-estimate this process's share, because
            # other processes could also have handle-only references that we
            # don't know about.
            vmo_node.area = int(float(vmo['committed_bytes']) /
                                (float(vmo['share_count']) + 1))

    # Create the aggregated VMO nodes.
    children = []
    for name, vmos in name_to_vmo.iteritems():
        if len(vmos) == 1 or name == UNNAMED_NAME:
            # Only one VMO with this name, or multiple VMOs with an empty name.
            # Add them as direct children.
            children.extend(vmos)
        else:
            # Create a parent VMO for all of these VMOs with the same name.
            parent_id = '{}/{}'.format(id_prefix, name)
            pnode = lookup(parent_id)
            pnode.name = '{}[{}]'.format(name, len(vmos))
            pnode.type = 'vmo'
            if hide_aggregated:
                pnode.area = sum(map(sum_area, vmos))
                # And then drop the vmo nodes on the ground (by not adding
                # them as children).
            else:
                # The area will be calculated from the children.
                pnode.children.extend(vmos)
            children.append(pnode)
    # TODO(dbort): Call out VMOs/aggregates that are only reachable via handle?

    process_node.children.extend(children)


def build_webtreemap(node):
    """Returns a JSON-able dict tree representing a Node tree.

    See
    https://github.com/evmar/webtreemap/blob/gh-pages/README.markdown#input-format
    For a description of this data format.

    Args:
        node: The Node at the root of the tree to walk
    Returns:
        A webtreemap-compatible dict representing the tree
    """
    return {
            'name': '{} ({})'.format(node.html_label(), format_size(node.area)),
            'data': {
                    '$area': node.area,
                    # TODO(dbort): Turn this on and style different node types
                    # if https://github.com/evmar/webtreemap/pull/15 is
                    # accepted. Would define a class like
                    # 'webtreemap-symbol-<type>' but there's a bug in
                    # webtreemap.js.
                    # '$symbol': node.type,
            },
            'children': map(build_webtreemap, node.children)
    }


def dump_html_table(node, depth=0, parent_area=None, total_area=None):
    """Returns an HTML representation of the tree.

    Args:
        node: The root of the tree to dump
        depth: The depth of the node. Use 0 for the root node.
        parent_area: The size of the parent node; used to show fractions.
                Use None for the root node.
        total_area: The total size of the tree; used to show fractions.
                Use None for the root node.
    Returns:
        A sequence of HTML lines, joinable by whitespace
    """
    lines = []

    if not depth:
        # We're the root node. Dump the headers.
        lines.extend([
                '<style>',
                'table#tree {',
                '    border-collapse: collapse;',
                '    border-spacing: 0;',
                '}',
                'table#tree tr:nth-child(even) {',
                '    background-color: #eee;',
                '}',
                'table#tree tr:nth-child(odd) {',
                '    background-color: #fff;',
                '}',
                'table#tree tr:hover {',
                '    background-color: #ff8;',
                '}',
                'table#tree td {',
                '    text-align: right;',
                '    padding-left: 1em;',
                '    padding-right: 1em;',
                '    font-family:Consolas,Monaco,Lucida Console,',
                '        Liberation Mono,DejaVu Sans Mono,',
                '        Bitstream Vera Sans Mono,Courier New,monospace;',
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
            '{label}</td>'.format(
                    indent=('|' + '&nbsp;' * 2) * depth,
                    label=node.html_label()),
            '<td>{fsize}</td>'.format(fsize=format_size(node.area)),
            '<td>{size}</td>'.format(size=node.area),
    ])

    if depth:
        # We're not the root node.
        pfrac = node.area / float(parent_area) if parent_area else 0
        tfrac = node.area / float(total_area) if total_area else 0
        for frac in (pfrac, tfrac):
            lines.extend([
                    ('<td>{pct:.3f}%&nbsp;'
                     '<progress value="{frac}"></progress></td>')
                    .format(pct=frac * 100, frac=frac)
            ])
    else:
        lines.append('<td></td>' * 2)
    lines.append('</tr>')

    if total_area is None:
        total_area = node.area

    # Append children by size, largest to smallest.
    def dump_child(child):
        return dump_html_table(child, depth=depth+1,
                               parent_area=node.area, total_area=total_area)

    children = sorted(node.children, reverse=True, key=lambda n: n.area)
    for line in [dump_child(c) for c in children]:
        lines.extend(line)

    if not depth:
        lines.append('</table>')

    return lines


def build_tree(dataset):
    """Builds a Node tree from a set of memgraph records.

    See
    https://fuchsia.dev/fuchsia-src/development/memory/memory#visualize_memory_usage
    for an example of generating memgraph JSON data.

    Args:
        dataset: A sequence of memgraph records, typically parsed from JSON
    Returns:
        The root of the new Node tree
    """
    ids_to_nodes.clear()  # Clear out the global registry.
    root_node = None
    root_job = None

    for record in dataset:
        record_type = record['type']
        # Only read certain types.
        if record_type not in ('kernel', 'j', 'p'):
            continue
        node = lookup(record['id'])
        node.type = record_type
        node.koid = record.get('koid', 0)
        node.name = record['name']
        if record_type == 'kernel':
            node.area = record.get('size_bytes', 0)
        elif record_type == 'j':
            if record['parent'].startswith('kernel/'):
                assert not root_job, 'Found multiple root jobs'
                root_job = node
        elif record_type == 'p':
            # Add the process's children, which will determine its area.
            populate_process(node, record)
        if not record['parent']:
            # The root node has an empty parent.
            assert not root_node, 'Found multiple root objects'
            root_node = node
        else:
            parent_node = lookup(record['parent'])
            parent_node.children.append(node)

    assert root_node, 'Did not find root object'
    assert root_job, 'Did not find root job'

    # A better name for physmem.
    lookup('kernel/physmem').name = 'All physical memory'

    # Sum up the job tree. Don't touch kernel entries, which already have
    # the correct sizes.
    sum_area(root_job)

    # The root job is usually named "root";
    # make it more clear that it's a job.
    root_job.name = 'root job'

    # Give users a hint that processes live in the VMO entry.
    kvmo_node = lookup('kernel/vmo')
    kvmo_node.name = 'VMOs/processes'

    # Create a fake entry to cover the portion of kernel/vmo that isn't
    # covered by the job tree.
    node = lookup('kernel/vmo/unknown')
    node.name = 'unknown (kernel & unmapped)'
    node.area = kvmo_node.area - root_job.area
    kvmo_node.children.append(node)

    return root_node


def print_html_document(root_node):
    """Prints to stdout an HTML document that visualizes a Node tree.

    Args:
        root_node: The Node at the root of the tree to walk
    """
    html = '''\
    <!DOCTYPE html>
    <title>Memory usage</title>
    <script>
    var kTree = %(json)s
    </script>
    <link rel='stylesheet' href='%(css)s'>
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
    .treemap-node-type { font-weight: bold; }
    /* Colorblind-safe colors from http://mkweb.bcgsc.ca/colorblind/ */
    .treemap-node-type-k { color: black; }
    .treemap-node-type-j { color: RGB(213, 94, 0); } /* Vermillion */
    .treemap-node-type-p { color: RGB(0, 114, 178); } /* Blue */
    .treemap-node-type-v { color: RGB(0, 158, 115); } /* Bluish green */
    </style>

    <h1>Memory usage</h1>

    <p>Click on a box to zoom in.  Click on the outermost box to zoom out.</p>

    <div id='map'></div>

    <script src='%(js)s'></script>
    <script>
    var map = document.getElementById('map');
    appendTreemap(map, kTree);
    </script>

    <ul style="list-style: none">
    <li><span class="treemap-node-type treemap-node-type-k">K</span>: Kernel memory
    <li><span class="treemap-node-type treemap-node-type-j">J</span>: Job
    <li><span class="treemap-node-type treemap-node-type-p">P</span>: Process
    <li><span class="treemap-node-type treemap-node-type-v">V</span>: VMO
    <ul style="list-style: none">
        <li> VMO names with <b>[<i>n</i>]</b> suffixes are aggregates of <i>n</i>
             VMOs that have the same name.
    </ul>
    </ul>

    <hr>
    %(table)s
    ''' % {
            'json': json.dumps(build_webtreemap(root_node)),
            'table': ' '.join(dump_html_table(root_node)),
            'css': os.path.join(FUCHSIA_DIR, 'scripts', 'third_party', 'webtreemap', 'webtreemap.css'),
            'js': os.path.join(FUCHSIA_DIR, 'scripts', 'third_party', 'webtreemap', 'webtreemap.js'),
    }
    print(textwrap.dedent(html))


def main():
    root_node = build_tree(json.load(sys.stdin))
    print_html_document(root_node)


if __name__ == '__main__':
    main()
