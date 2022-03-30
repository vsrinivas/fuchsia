#!/usr/bin/env fuchsia-vendored-python
#
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import re
import sys

from snapshot import Snapshot

group_specs = [
    ["[scudo]", "scudo:.*"],
    ["[relro]", "relro:.*"],
    ["[stacks]", "thrd_t:0x.*|initial-thread|pthread_t:0x.*"],
    ["[data]", "data[0-9]*:.*"],
    ["[blobs]", "blob-[0-9a-f]+"],
    ["uncompressed-bootfs", "uncompressed-bootfs"],
]

for gs in group_specs:
    gs[1] = re.compile(gs[1])


def main(args):
    snapshot = Snapshot.FromJSONFile(
        sys.stdin) if args.snapshot is None else Snapshot.FromJSONFilename(
            args.snapshot)
    vmo_to_count = {}
    for p in snapshot.processes.values():
        for v in p.vmos:
            if v.koid in vmo_to_count:
                vmo_to_count[v.koid] += 1
            else:
                vmo_to_count[v.koid] = 1
    nodes = []
    snaphot_name = 'Snapshot'
    kernel_name = 'Kernel'
    nodes.append(["Orphaned", snaphot_name, 0])
    nodes.append(["Orphaned VMOs", "Orphaned", snapshot.orphaned])
    nodes.append([kernel_name, snaphot_name, 0])
    nodes.append(["Wired", kernel_name, snapshot.kernel.wired])
    nodes.append(["Heap", kernel_name, snapshot.kernel.total_heap])
    nodes.append(["IPC", kernel_name, snapshot.kernel.ipc])
    nodes.append(["Other", kernel_name, snapshot.kernel.other])
    nodes.append(["MMU", kernel_name, snapshot.kernel.mmu])
    for p in snapshot.processes.values():
        process_name = "%s<%d>" % (p.name, p.koid)
        nodes.append([process_name, snaphot_name, 0])
        groups = {}
        for v in p.vmos:
            group_name = None
            for gs in group_specs:
                if gs[1].match(v.name):
                    group_name = "%s<%d>" % (gs[0], p.koid)
                    if gs[0] not in groups:
                        nodes.append([group_name, process_name, 0])
                        groups[gs[0]] = True
                    break
            vmo_name = "%s<%d:%d>" % (v.name, p.koid, v.koid)
            nodes.append([
                vmo_name,
                process_name if group_name is None else group_name,
                float(v.committed_bytes) / vmo_to_count[v.koid]
            ])
    template = '''\
<html>
  <head>
    <script type="text/javascript" src="https://www.gstatic.com/charts/loader.js"></script>
    <script type="text/javascript">
      google.charts.load('current', {'packages':['treemap']});
      google.charts.setOnLoadCallback(drawChart);

      function drawChart() {
        var data = google.visualization.arrayToDataTable([
          ['Process','Parent','Size (MB)'],
          ['Snapshot',null,0],
            %s
        ]);

        tree = new google.visualization.TreeMap(document.getElementById('chart_div'));

        tree.draw(data, {
          headerHeight: 15,
          fontColor: 'black',
          generateTooltip: showTooltip,
          maxDepth: 2,
          enableHighlight: true,
          minHighlightColor: '#8c6bb1',
          midHighlightColor: '#9ebcda',
          maxHighlightColor: '#edf8fb',
          minColor: '#009688',
          midColor: '#f7f7f7',
          maxColor: '#ee8100'
        });

        function showTooltip(row, size, value) {
          return '<div style="background:#fd9; padding:10px; border-style:solid">' + data.getValue(row, 0) + ' ' + size.toFixed(2) + 'MB</div>';
        }

      }
    </script>
  </head>
  <body>
    <div id="chart_div" style="width: 100%%; height: 100%%;"></div>
  </body>
</html>
'''
    node_strings = '\n'.join([
        "[\'%s\',\'%s\',%.2g]," % (n[0], n[1], round(n[2] / (1024.0 * 1024), 2))
        for n in nodes
    ])
    print(template % node_strings)


def get_arg_parser():
    parser = argparse.ArgumentParser(description='Convert snapshot to tree.')
    parser.add_argument('-s', '--snapshot')
    return parser


if __name__ == '__main__':
    main(get_arg_parser().parse_args())
