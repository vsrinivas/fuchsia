# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import functools
import json


class VMO(object):

    def __init__(self, v, names):
        self.koid = int(v[0])
        self.name = names[v[1]]
        self.parent_koid = int(v[2])
        self.committed_bytes = int(v[3])
        self.allocated_bytes = int(v[4])


class Process(object):

    def __init__(self, p, vmos):
        self.koid = int(p[0])
        self.name = p[1]
        self.vmos = [vmos[int(v)] for v in p[2]]


class Kernel(object):

    def __init__(self, k):
        self.wired = k['wired']
        self.total_heap = k['total_heap']
        self.mmu = k['mmu']
        self.ipc = k['ipc']
        self.other = k['other']
        self.vmo = k['vmo']


class Snapshot(object):

    @classmethod
    def FromJSON(cls, snap_json):
        return Snapshot(snap_json)

    @classmethod
    def FromJSONFile(cls, snap_file):
        return cls.FromJSON(json.load(snap_file))

    @classmethod
    def FromJSONString(cls, snap_string):
        return cls.FromJSON(json.loads(snap_string))

    @classmethod
    def FromJSONFilename(cls, snap_filename):
        with open(snap_filename) as snap_file:
            return cls.FromJSONFile(snap_file)

    def __init__(self, snap_json):
        vmo_names = snap_json['VmoNames']
        self.kernel = Kernel(snap_json['Kernel'])
        self.vmos = {v[0]: VMO(v, vmo_names) for v in snap_json['Vmos'][1:]}
        self.processes = {
            p[0]: Process(p, self.vmos) for p in snap_json['Processes'][1:]
        }
        vmo_bytes = 0
        for v in self.vmos.values():
            vmo_bytes += v.committed_bytes
        self.orphaned = self.kernel.vmo - vmo_bytes
