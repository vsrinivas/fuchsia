# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from difflib import SequenceMatcher
from typing import List, Optional, Sequence, Tuple, FrozenSet, Set, Dict, NamedTuple

from difl.ir import Library, Struct, StructMember, Type, Table, Union, Protocol, Enum
from difl.changes import *
from difl.intersection import intersect_changes
from difl.type import compare_types


class StructLayoutDifferences(NamedTuple):
    unmoved: List[str]
    moved: List[str]
    resized: List[str]
    renamed: List[Tuple[str, str]]
    added: List[str]
    removed: List[str]
    split: List[Tuple[str, List[str]]]
    joined: List[Tuple[List[str], str]]

def _member_bytes(names: Set[str], members: Dict[str, StructMember]) -> Dict[str, FrozenSet[int]]:
    '''
    Takes a set of member names and a dict of named struct members and
    returns a dict of member names to a set of the byte offsets that the
    members occupy.
    '''
    member_bytes: Dict[str, FrozenSet[int]] = {}
    for name in names:
        member = members[name]
        member_bytes[name] = frozenset(range(member.offset, member.offset+member.size))
    return member_bytes

def _overlapping(outer: FrozenSet[int], inners: Dict[str, FrozenSet[int]]) -> Optional[List[str]]:
    '''
    Takes an outer set and a dict of named disjunct inner sets.
    If there are one or more inner sets whose union is equal to the outer set, return their names.
    '''
    intersecting: List[str] = []
    intersection: Set[int] = set()
    for name in inners.keys():
        if outer.issuperset(inners[name]):
            intersecting.append(name)
            intersection.update(inners[name])
    if intersection == outer:
        return intersecting
    else:
        return None

def _all_overlapping(outers:Dict[str, FrozenSet[int]] , inners: Dict[str, FrozenSet[int]]) -> List[Tuple[str, List[str]]]:
    '''
    For every outer set named in look for some inner sets whose union equals the outer set.
    Remove matched sets from the supplied dictionaries and return a list tuples of the matches.
    '''
    overlaps: List[Tuple[str, List[str]]] = []
    for name in frozenset(outers.keys()):
        overlapping = _overlapping(outers[name], inners)
        if overlapping is None: continue
        overlaps.append((name, overlapping))
        outers.pop(name)
        for o in overlapping:
            inners.pop(o)
    return overlaps

def compare_struct_layout(before: Struct,
                          after: Struct) -> StructLayoutDifferences:
    before_members = {m.name: m for m in before.members}
    after_members = {m.name: m for m in after.members}
    before_names = set(before_members.keys())
    after_names = set(after_members.keys())
    common_names = before_names.intersection(after_names)
    only_before_names = before_names - common_names
    only_after_names = after_names - common_names

    # categorize struct members that exist before and after into moved and unmoved
    unmoved: List[str] = []
    moved: List[str] = []
    resized: List[str] = []
    for name in common_names:
        before_member: StructMember = before_members[name]
        after_member: StructMember = after_members[name]
        if before_member.offset != after_member.offset:
            moved.append(name)
        elif before_member.size != after_member.size:
            resized.append(name)
        else:
            unmoved.append(name)

    # look for members that have been renamed
    renamed: List[Tuple[str, str]] = []
    for name in frozenset(only_before_names):
        before_member = before_members[name]
        # look for a member whose location matches
        for after_member in [after_members[n] for n in only_after_names]:
            if before_member.offset == after_member.offset and \
                 before_member.size == after_member.size:
                renamed.append((before_member.name, after_member.name))
                # remove these members from further consideration
                only_before_names.remove(before_member.name)
                only_after_names.remove(after_member.name)

    # TODO: split and join logic should probably be limited to integer primitives and enums

    before_bytes = _member_bytes(only_before_names, before_members)
    after_bytes = _member_bytes(only_after_names, after_members)

    # look for before members whose space is wholy overlapping with some after members
    split: List[Tuple[str, List[str]]] = _all_overlapping(before_bytes, after_bytes)
    # look for after members whose space is wholy overlapping with some before members
    joined: List[Tuple[List[str], str]] = [(y,x) for x,y in _all_overlapping(after_bytes, before_bytes)]

    return StructLayoutDifferences(
        unmoved=unmoved,
        moved=moved,
        resized=resized,
        renamed=renamed,
        split=split,
        joined=joined,
        removed=list(before_bytes.keys()),
        added=list(after_bytes.keys()))


def struct_changes(before: Struct, after: Struct, identifier_compatibility: Dict[str, bool]) -> List[Change]:
    changes: List[Change] = []

    if before.size != after.size:
        changes.append(StructSizeChanged(before, after))

    layout_diff = compare_struct_layout(before, after)

    before_members: Dict[str, StructMember] = {
        m.name: m
        for m in before.members
    }
    after_members: Dict[str, StructMember] = {m.name: m for m in after.members}

    for name in layout_diff.unmoved:
        before_member = before_members[name]
        after_member = after_members[name]
        equal, compatible = compare_types(before_member.type,
                                          after_member.type, identifier_compatibility)
        if not equal:
            changes.append(
                StructMemberTypeChanged(before_member, after_member,
                                        compatible))

    for name in layout_diff.moved:
        before_member = before_members[name]
        after_member = after_members[name]
        changes.append(StructMemberMoved(before_member, after_member))
        equal, compatible = compare_types(before_member.type,
                                          after_member.type, identifier_compatibility)
        if not equal:
            changes.append(
                StructMemberTypeChanged(before_member, after_member,
                                        compatible))

    for name in layout_diff.resized:
        before_member = before_members[name]
        after_member = after_members[name]
        changes.append(StructMemberSizeChanged(before_member, after_member))
        equal, compatible = compare_types(before_member.type,
                                          after_member.type, identifier_compatibility)
        assert not equal
        changes.append(
            StructMemberTypeChanged(before_member, after_member, compatible))

    for before_name, after_name in layout_diff.renamed:
        changes.append(
            StructMemberRenamed(before_members[before_name],
                                after_members[after_name]))

    for name in layout_diff.added:
        changes.append(StructMemberAdded(before, after_members[name]))

    for name in layout_diff.removed:
        changes.append(StructMemberRemoved(before_members[name], after))

    for before_name, after_names in layout_diff.split:
        changes.append(
            StructMemberSplit(before_members[before_name],
                              [after_members[n] for n in after_names]))

    for before_names, after_name in layout_diff.joined:
        changes.append(
            StructMemberJoined([before_members[n] for n in before_names],
                               after_members[after_name]))

    identifier_compatibility[before.name] = (len(changes) == 0)
    return changes
