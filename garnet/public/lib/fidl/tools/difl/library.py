# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from typing import List

from difl.ir import Library
from difl.changes import Change
from difl.protocol import protocol_changes
from difl.struct import struct_changes
from difl.intersection import intersect_changes


def library_changes(before: Library, after: Library) -> List[Change]:
    changes: List[Change] = []
    changes.extend(
        intersect_changes(before.protocols, after.protocols, protocol_changes))
    changes.extend(
        intersect_changes(before.structs, after.structs, struct_changes))
    return changes
