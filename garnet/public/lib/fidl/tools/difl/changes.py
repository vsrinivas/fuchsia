# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from typing import List, Optional

from difl.ir import *


class Change:
    '''Represents a change between two versions of a FIDL library.'''
    before: Optional[Declaration]
    after: Optional[Declaration]

    def __init__(self, before: Optional[Declaration],
                 after: Optional[Declaration]):
        self.before = before
        self.after = after

    @property
    def name(self) -> str:
        return type(self).__name__

    def __lt__(self, other: 'Change'):
        '''Comparison function so that changes can be sorted by source location.'''
        if self.after is not None and other.after is not None:
            return self.after.location < other.after.location
        if self.before is None:
            return True
        if other.before is None:
            return False
        return self.before.location < other.before.location


class ClassifiedChange:
    change: Change
    hard: bool
    message: str

    def __init__(self, change, hard, message):
        self.change = change
        self.hard = hard
        self.message = message


#### Generic Declaration Changes
class DeclAdded(Change):
    pass


class DeclRemoved(Change):
    pass


#### Struct Changes


class StructSizeChanged(Change):
    pass


class StructMemberAdded(Change):
    pass


class StructMemberRemoved(Change):
    pass


class StructMemberMoved(Change):
    pass


class StructMemberSizeChanged(Change):
    pass


class StructMemberTypeChanged(Change):
    compatible: bool

    def __init__(self, before: StructMember, after: StructMember,
                 comptatible: bool):
        super().__init__(before, after)
        self.compatible = comptatible


class StructMemberRenamed(Change):
    pass


class StructMemberSplit(Change):
    afters: List[StructMember]

    def __init__(self, before: StructMember, afters: List[StructMember]):
        super().__init__(before, afters[0].struct)
        self.afters = afters


class StructMemberJoined(Change):
    befores: List[StructMember]

    def __init__(self, befores: List[StructMember], after: StructMember):
        super().__init__(befores[0].struct, after)
        self.befores = befores


#### Table Changes


class TableMemberAdded(Change):
    pass


class TableMemberRemoved(Change):
    pass


class TableMemberReserved(Change):
    pass


class TableMemberUnreserved(Change):
    pass


class TableMemberRenamed(Change):
    pass


class TableMemberTypeChanged(Change):
    compatible: bool

    def __init__(self, before: TableMember, after: TableMember,
                 comptatible: bool):
        super().__init__(before, after)
        self.compatible = comptatible


#### Protocol Changes
class MethodOrdinalChanged(Change):
    pass


class EventBecameMethod(Change):
    pass


class MethodBecameEvent(Change):
    pass


class MethodGainedResponse(Change):
    pass


class MethodLostResponse(Change):
    pass
