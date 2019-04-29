# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from typing import Sequence, TypeVar, Generic, List, Tuple, Callable, Dict

from difl.ir import Declaration
from difl.changes import Change, DeclAdded, DeclRemoved

D = TypeVar('D', bound=Declaration)


def intersect_changes(before: Sequence[D], after: Sequence[D],
                      compare: Callable[[D, D, Dict[str, bool]], List[Change]],
                      identifier_compatibility: Dict[str, bool],
                      include_decl_added_and_decl_removed=True) -> List[Change]:
    # dictionaries of declarations by name
    before_by_name = {d.name: d for d in before}
    after_by_name = {d.name: d for d in after}
    # immutable sets of declaration names
    before_names = frozenset(before_by_name.keys())
    after_names = frozenset(after_by_name.keys())

    changes: List[Change] = []

    if include_decl_added_and_decl_removed:
        for decl in [after_by_name[n] for n in after_names - before_names]:
            changes.append(DeclAdded(before=None, after=decl))
        for decl in [before_by_name[n] for n in before_names - after_names]:
            changes.append(DeclRemoved(before=decl, after=None))
    for before_decl, after_decl in [
        (before_by_name[n], after_by_name[n])
            for n in before_names.intersection(after_names)
    ]:
        changes.extend(compare(before_decl, after_decl, identifier_compatibility))

    return changes
