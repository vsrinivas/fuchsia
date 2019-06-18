# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from typing import List, Optional, Dict

from difl.ir import Method, Argument, Library, Protocol
from difl.changes import *
from difl.intersection import intersect_changes
from difl.struct import struct_changes
from difl.comparator import Comparator


def method_changes(before: Method, after: Method,
                   comparator: Comparator) -> List[Change]:
    changes: List[Change] = []
    # Ordinal change
    if before.ordinal != after.ordinal:
        changes.append(MethodOrdinalChanged(before, after))
    # Type of method
    if before.is_event() and not after.is_event():
        changes.append(EventBecameMethod(before, after))
    elif not before.is_event() and after.is_event():
        changes.append(MethodBecameEvent(before, after))
    elif before.is_one_way() and not after.is_one_way():
        changes.append(MethodGainedResponse(before, after))
    elif not before.is_one_way() and after.is_one_way():
        changes.append(MethodLostResponse(before, after))

    # Arguments
    before_request = before.request()
    after_request = after.request()
    if before_request is not None and after_request is not None:
        changes = changes + struct_changes(before_request, after_request,
                                           comparator)

    before_response = before.response()
    after_response = after.response()
    if before_response is not None and after_response is not None:
        changes = changes + struct_changes(before_response, after_response,
                                           comparator)

    return changes


def protocol_changes(before: Protocol, after: Protocol,
                     comparator: Comparator) -> List[Change]:
    return intersect_changes(before.methods, after.methods, method_changes,
                             comparator)
