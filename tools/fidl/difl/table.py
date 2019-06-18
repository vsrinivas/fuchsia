# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from typing import List, Dict

from difl.ir import Table
from difl.changes import *
from difl.comparator import Comparator


def table_changes(before: Table, after: Table,
                  comparator: Comparator) -> List[Change]:
    changes: List[Change] = []

    before_members = {m.ordinal: m for m in before.members}
    after_members = {m.ordinal: m for m in after.members}
    ordinals = frozenset([*before_members.keys(), *after_members.keys()])

    for ordinal in ordinals:
        if ordinal not in before_members:
            changes.append(TableMemberAdded(before, after_members[ordinal]))
            continue
        if ordinal not in after_members:
            changes.append(TableMemberRemoved(before_members[ordinal], after))
            continue

        before_member = before_members[ordinal]
        after_member = after_members[ordinal]
        if not before_member.reserved and after_member.reserved:
            changes.append(TableMemberReserved(before_member, after_member))
            continue
        if before_member.reserved and not after_member.reserved:
            changes.append(TableMemberUnreserved(before_member, after_member))
            continue

        if before_member.name != after_member.name:
            changes.append(TableMemberRenamed(before_member, after_member))

        if before_member.reserved and after_member.reserved:
            continue

        if not comparator.constraints_match(before_member.type,
                                            after_member.type):
            soft = comparator.shapes_match(before_member.type,
                                           after_member.type)
            changes.append(
                TableMemberTypeChanged(before_member, after_member, not soft))

    return changes
