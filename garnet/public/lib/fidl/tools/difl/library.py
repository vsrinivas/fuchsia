# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from typing import List, Tuple, Iterable, Dict

from difl.ir import *
from difl.changes import Change, DeclAdded, DeclRemoved
from difl.enums import enum_changes
from difl.protocol import protocol_changes
from difl.struct import struct_changes
from difl.table import table_changes
from difl.intersection import intersect_changes
from difl.comparator import Comparator


def libraries_changes(before: Libraries, after: Libraries,
                      comparator: Comparator) -> List[Change]:
    # Only analyze libraries that exist before and after
    return intersect_changes(
        before,
        after,
        library_changes,
        comparator,
        include_decl_added_and_decl_removed=False)


def declaration_changes(before: Declaration, after: Declaration,
                        comparator: Comparator) -> List[Change]:
    if isinstance(before, Enum) and isinstance(after, Enum):
        return enum_changes(before, after, comparator)
    if isinstance(before, Struct) and isinstance(after, Struct):
        return struct_changes(before, after, comparator)
    if isinstance(before, Table) and isinstance(after, Table):
        return table_changes(before, after, comparator)
    if isinstance(before, Protocol) and isinstance(after, Protocol):
        return protocol_changes(before, after, comparator)

    if type(before) != type(after):
        # TODO: create DeclarationTypeChanged or something
        #print('TYPE CHANGED %r %r' % (before, after))
        return []

    # TODO: support other declaration types
    #print('UNHANDLED DECLARATION: %r %r' % (before, after))
    return []


def library_changes(before: Library, after: Library,
                    comparator: Comparator) -> List[Change]:
    changes: List[Change] = []

    for name in after.declaration_order:
        after_decl = after.declarations[name]
        before_decl = before.declarations.get(name)
        if before_decl is None:
            changes.append(DeclAdded(before=None, after=after_decl))
            continue
        changes.extend(
            declaration_changes(before_decl, after_decl, comparator))
    for name, before_decl in before.declarations.items():
        if name not in after.declarations:
            changes.append(DeclRemoved(before=before_decl, after=None))
    return changes
