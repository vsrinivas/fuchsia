from difl.ir import Library, Struct, StructMember, Type, Table, Union, Protocol, Enum
from typing import List, Optional, Sequence, Tuple, Set, Dict


# TODO: does this belong in ir.py?
def type_size(t: Type) -> int:
    '''Inline size of a type'''
    if t.is_primitive:
        if t['subtype'] in {'bool', 'int8', 'uint8'}: return 1
        if t['subtype'] in {'int16', 'uint16'}: return 2
        if t['subtype'] in {'int32', 'uint32', 'float32'}: return 4
        if t['subtype'] in {'int64', 'uint64', 'float64'}: return 8
        raise Exception('Unknown primitive subtype %r' % t['subtype'])
    if t.kind == 'array':
        return t['element_count'] * type_size(t.element_type)
    if t.kind == 'vector' or t.kind == 'string':
        return 16
    if t.kind == 'handle' or t.kind == 'request':
        return 4
    assert t.kind == 'identifier'
    resolved = t.library.libraries.find(t['identifier'])

    # TODO: xunion
    if isinstance(resolved, (Struct, Table, Union)):
        if t.is_nullable:
            return 8
        return resolved['size']

    if isinstance(resolved, Enum):
        return type_size(resolved.type)

    if isinstance(resolved, Protocol):
        return 4  # it's a handle

    raise Exception("Don't know how to calculate the size of %r" % resolved)


def compare_types(before: Type, after: Type) -> Tuple[bool, bool]:
    '''
    Compares two types and returns a tuple of two bools indicating equivalence and compatibility.
    '''
    # identical types are identical
    if before == after:
        return (True, True)

    # different sized types are incompatible
    if type_size(before) != type_size(after):
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
        return compare_types(before.element_type, after.element_type)

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
        return compare_types(before.element_type, after.element_type)

    if before.kind == 'identifier':
        # TODO: implement this
        raise NotImplementedError

    return (False, False)
