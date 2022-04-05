#!/usr/bin/env fuchsia-vendored-python
#
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import functools
import re
import sys

from digest import Digest
from snapshot import Snapshot

group_specs = [
    ["[scudo]", "scudo:.*"],
    ["[relro]", "relro:.*"],
    ["[stacks]", "thrd_t:0x.*|initial-thread|pthread_t:0x.*"],
    ["[data]", "data[0-9]*:.*"],
    ["[blobs]", "blob-[0-9a-f]+"],
    ["[inactive-blobs]", "inactive-blob-[0-9a-f]+"],
    ["[mrkls]", "mrkl-[0-9a-f]+"],
    ["[uncompressed-bootfs]", "uncompressed-bootfs"],
]

for gs in group_specs:
    gs[1] = re.compile(gs[1])


def output_html(buckets):
    nodes = []
    node_id = 1
    snaphot_name = 'Snapshot'
    for b in buckets:
        if b.size == 0:
            continue
        nodes.append([b.name, snaphot_name, 0])
        for processes, vmos in b.processes.items():
            process_name = "[%d] %s" % (node_id, ",".join([p.name for p in processes]))
            node_id += 1
            nodes.append([process_name, b.name, 0])
            groups = {}
            for v in vmos:
                group_name = None
                for gs in group_specs:
                    if gs[1].match(v.name):
                        if gs[0] in groups:
                            group_name = groups[gs[0]]
                        else:
                            group_name = "[%d] %s" % (node_id, gs[0])
                            node_id += 1
                            groups[gs[0]] = group_name
                            nodes.append([group_name, process_name, 0])
                        break
                vmo_name = "%s<%d>" % (v.name, v.koid)
                nodes.append([
                    vmo_name,
                    process_name if group_name is None else group_name,
                    v.committed_bytes
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



def main(args):
    snapshot = Snapshot.FromJSONFile(
        sys.stdin) if args.snapshot is None else Snapshot.FromJSONFilename(
            args.snapshot)
    digest = Digest.FromJSONFilename(snapshot, args.digest)

    buckets = sorted(digest.buckets.values(), key=lambda b: b.name)
    if (args.output == "csv"):
        for bucket in buckets:
            print("%s, %d" % (bucket.name, bucket.size))
    elif (args.output == "html"):
        output_html(buckets)
    else:
        total = 0
        for bucket in buckets:
            if bucket.size == 0:
                continue
            print("%s: %s" % (bucket.name, fmt_size(bucket.size)))
            total += bucket.size
            if bucket.name != "Undigested" and bucket.processes and args.verbose:
                entries = []
                for processes, vmos in bucket.processes.items():
                    size = sum([v.committed_bytes for v in vmos])
                    assert size
                    assert len(processes) == 1
                    entries.append((processes[0].name, size))
                # Reverse sort by size.
                entries.sort(key=lambda t: t[1], reverse=True)
                for name, size in entries:
                    print("\t%s: %s" % (name, fmt_size(size)))
        print("\nTotal: %s" % (fmt_size(total)))

        undigested = digest.buckets["Undigested"]
        if undigested.processes:
            print("\nUndigested:")
            entries = []
            for processes, vmos in undigested.processes.items():
                size = sum([v.committed_bytes for v in vmos])
                assert size
                entries.append((processes, size, vmos))
            # Sort by largest sharing pool, then size
            def cmp(entry_a, entry_b):
                if len(entry_b[0]) - len(entry_a[0]):
                    return len(entry_b[0]) - len(entry_a[0])
                return entry_b[1] - entry_a[1]

            entries.sort(key=functools.cmp_to_key(cmp))
            for processes, size, vmos in entries:
                names = [p.full_name for p in processes]
                print("\t%s: %s" % (" ,".join(names), fmt_size(size)))
                for process in processes:
                    if args.verbose:
                        # Group by VMO name, store count and total.
                        print("\t\t%s VMO Summaries:" % process.full_name)
                        groups = {}
                        for v in vmos:
                            cnt, ttl = groups.get(v.name, (0, 0))
                            groups[v.name] = (cnt + 1, ttl + v.committed_bytes)

                        for name, (count,
                                   total) in sorted(groups.items(),
                                                    key=lambda kv: kv[1][1],
                                                    reverse=True):
                            print(
                                "\t\t\t%s (%d): %s" %
                                (name, count, fmt_size(total)))
                    elif args.extra_verbose:
                        # Print "em all
                        print("\t\t%s VMOs:" % process.full_name)
                        for v in sorted(vmos, key=lambda v: v.name):
                            print(
                                "\t\t\t%s[%d]: %s" %
                                (v.name, v.koid, fmt_size(v.committed_bytes)))


def fmt_size(num, suffix="B"):
    for unit in ["", "Ki", "Mi"]:
        if abs(num) < 1024.0:
            return "%.4g%s%s" % (num, unit, suffix)
        num /= 1024.0
    return "%.4g%s%s" % (num, "Gi", suffix)


def get_arg_parser():
    parser = argparse.ArgumentParser(description="Convert snapshot to digest.")
    parser.add_argument("-s", "--snapshot")
    parser.add_argument("-d", "--digest")
    parser.add_argument(
        "-o", "--output", default="human", choices=["csv", "human", "html"])
    parser.add_argument("-v", "--verbose", action="store_true")
    parser.add_argument("-vv", "--extra_verbose", action="store_true")
    return parser


if __name__ == "__main__":
    main(get_arg_parser().parse_args())
