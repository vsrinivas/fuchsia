# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from typing import List, Optional, Sequence, Tuple, FrozenSet, Set, Dict, NamedTuple

from difl.ir import Library, Struct, StructMember, Type, Table, Union, Protocol, Enum
from difl.changes import *
from difl.comparator import Comparator


def enum_changes(before: Enum, after: Enum,
                 comparator: Comparator) -> List[Change]:
    changes: List[Change] = []

    # TODO: actually compare enums

    return changes
