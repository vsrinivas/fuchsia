# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from difl.ir import *
from typing import List, Optional, Sequence, Tuple, Set, Dict


def compare_types(before: Type, after: Type, identifier_compatibility: Dict[str, bool]) -> Tuple[bool, bool]:
    '''
    Compares two types and returns a tuple of two bools indicating equivalence and compatibility.
    '''
    # identical types are identical
    if before == after and before.kind != 'identifier':
        return (True, True)

    # different sized types are incompatible
    if before.inline_size != after.inline_size:
        return (False, False)

    # nonidentical handle types are compatible with each other but incompatible with others
    # TODO: nullability on handles?
    if before.is_handle and after.is_handle:
        return (False, True)
    if before.is_handle ^ after.is_handle:
        return (False, False)

    # TODO: compatibility between enums and integer types?
    if before.is_primitive and after.is_primitive:
        integer_subtypes = {
            'bool', 'int8', 'uint8', 'int16', 'uint16', 'int32', 'uint32'
        }
        if before['subtype'] in integer_subtypes and after['subtype'] in integer_subtypes:
            return (False, True)
        else:
            # float types are incompatible with each other and with integer types
            return (False, False)

    if before.is_primitive ^ after.is_primitive:
        # primitive types are incompatible
        return (False, False)

    # TODO: compatibility between strings and vector<int8>, vector<uint8>, vector<bool>

    if before.kind != after.kind:
        return (False, False)

    if before.kind == 'array':
        if before['element_count'] != after['element_count']:
            return (False, False)
        # if the counts are the same we can just compare the element types
        return compare_types(before.element_type, after.element_type, identifier_compatibility)

    if before.kind == 'string':
        if after.get('maybe_element_count', 2**32) > before.get(
                'maybe_element_count', 2**32):
            # increasing max length is a breaking change
            return (False, False)
        return (False, True)

    if before.kind == 'vector':
        if after.get('maybe_element_count', 2**32) > before.get(
                'maybe_element_count', 2**32):
            # increasing max length is a breaking change
            return (False, False)
        return compare_types(before.element_type, after.element_type, identifier_compatibility)

    assert before.kind == 'identifier' and after.kind == 'identifier'

    before_decl = before.library.libraries.find(before['identifier'])
    after_decl = after.library.libraries.find(after['identifier'])

    # TODO: are bits and enums soft compatible?
    if type(before_decl) != type(after_decl):
        return (False, False)

    if before.is_nullable != after.is_nullable:
        if isinstance(before_decl, (XUnion, Protocol)):
          # Nullability is soft change for xunions and protocols
          return (False, True)
        else:
          # No other types should have nullability
          assert isinstance(before_decl, (Struct, Union))
          # Nullability changes layout for structs and unions
          return (False, False)

    if before['identifier'] == after['identifier']:
        compat = identifier_compatibility[before['identifier']]
        return (compat, compat)

    raise NotImplementedError('Implement comparison for %r' % (type(before_decl)))

    return (False, False)
