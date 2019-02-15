# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from typing import Sequence, TypeVar, Generic, List, Tuple, Callable
from dataclasses import dataclass

from difl.ir import Declaration
from difl.changes import Change, DeclAdded, DeclRemoved

D = TypeVar('D', bound=Declaration)


class DeclarationIntersection(Generic[D]):
    '''Find which declarations are removed, shared or added between the lists supplied'''

    def __init__(self, old: Sequence[D], new: Sequence[D]):
        # dictionaries of declarations by name
        olds = {d.name: d for d in old}
        news = {d.name: d for d in new}
        # immutable sets of declaration names
        old_names = frozenset(olds.keys())
        new_names = frozenset(news.keys())

        self.removed = [olds[n] for n in old_names - new_names]
        self.common = [(olds[n], news[n])
                       for n in old_names.intersection(new_names)]
        self.added = [news[n] for n in new_names - old_names]


def intersect_changes(before: Sequence[D], after: Sequence[D],
                      compare: Callable[[D, D], List[Change]]) -> List[Change]:
    # dictionaries of declarations by name
    before_by_name = {d.name: d for d in before}
    after_by_name = {d.name: d for d in after}
    # immutable sets of declaration names
    before_names = frozenset(before_by_name.keys())
    after_names = frozenset(after_by_name.keys())

    changes: List[Change] = []

    for decl in [after_by_name[n] for n in after_names - before_names]:
        changes.append(DeclAdded(before=None, after=decl))
    for decl in [before_by_name[n] for n in before_names - after_names]:
        changes.append(DeclRemoved(before=decl, after=None))
    for before_decl, after_decl in [
        (before_by_name[n], after_by_name[n])
            for n in before_names.intersection(after_names)
    ]:
        changes.extend(compare(before_decl, after_decl))

    return changes
