# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import re
import snapshot


def re_from_string_or_list(input):
    if input is None:
        return None
    re_str = input if isinstance(input, str) else '|'.join(input)
    return re.compile(re_str)

class BucketMatch(object):

    def __init__(self, name, process, vmo):
        self.name = name
        self.process = re_from_string_or_list(process)
        self.vmo = re_from_string_or_list(vmo)


class Bucket(object):

    def __init__(self, name):
        self.name = name
        self.vmos = []
        self.size = 0


class Digest(object):

    @classmethod
    def FromMatchSpecs(cls, snapshot, match_specs):
        return Digest(snapshot,
                      [BucketMatch(m[0], m[1], m[2]) for m in match_specs])

    @classmethod
    def FromJSON(cls, snapshot, match_json):
        return Digest(snapshot, [
            BucketMatch(m["name"], m["process"] if "process" in m else None,
                        m["vmo"] if "vmo" in m else None) for m in match_json
        ])

    @classmethod
    def FromJSONFile(cls, snapshot, match_file):
        return cls.FromJSON(snapshot, json.load(match_file))

    @classmethod
    def FromJSONString(cls, snapshot, match_string):
        return cls.FromJSON(snapshot, json.loads(match_string))

    @classmethod
    def FromJSONFilename(cls, snapshot, match_filename):
        with open(match_filename) as match_file:
            return cls.FromJSONFile(snapshot, match_file)

    def __init__(self, snapshot, bucket_matches):
        self.buckets = {bm.name: Bucket(bm.name) for bm in bucket_matches}
        undigested_vmos = snapshot.vmos.copy()
        for p in snapshot.processes.values():
            for bm in bucket_matches:
                if bm.process is not None and bm.process.fullmatch(
                        p.name) == None:
                    continue
                for v in p.vmos:
                    if v.koid not in undigested_vmos:
                        continue
                    if bm.vmo is not None and bm.vmo.fullmatch(v.name) == None:
                        continue
                    self.buckets[bm.name].vmos.append(v)
                    self.buckets[bm.name].size += v.committed_bytes
                    del undigested_vmos[v.koid]
        undigested = Bucket("Undigested")
        undigested.vmos = undigested_vmos.values()
        for v in undigested.vmos:
            undigested.size += v.committed_bytes
        self.buckets["Undigested"] = undigested
        kernel = Bucket("Kernel")
        kernel.size = snapshot.kernel.wired + snapshot.kernel.total_heap + snapshot.kernel.mmu + snapshot.kernel.ipc + snapshot.kernel.other
        self.buckets["Kernel"] = kernel
        orphaned = Bucket("Orphaned")
        orphaned.size = snapshot.orphaned
        self.buckets["Orphaned"] = orphaned
