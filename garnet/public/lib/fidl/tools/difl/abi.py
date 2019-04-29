# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from typing import List, Optional, Iterator
from difl.ir import *
from difl.changes import *
from difl.struct import Struct, StructMember


def _describe_struct(struct: Optional[Declaration]) -> str:
    '''Describe a FIDL struct declaration in a way that might make sense to a developer.'''
    assert struct is not None
    assert isinstance(struct, Struct)

    if struct.method is None:
        return 'struct {}'.format(struct.name)
    else:
        if struct.request:
            return 'method {} request'.format(struct.method.name)
        else:
            return 'method {} response'.format(struct.method.name)


def _describe_decl(decl: Optional[Declaration]) -> str:
    assert decl is not None
    if isinstance(decl, Struct):
        return _describe_struct(decl)
    elif isinstance(decl, StructMember):
        return 'struct member {}.{}'.format(decl.struct.name, decl.name)
    elif isinstance(decl, Protocol):
        return 'protocol {}'.format(decl.name)
    elif isinstance(decl, Method):
        return 'method {}'.format(decl.name)
    else:
        raise Exception("Don't know how to describe {} {!r}".format(
            type(decl).__name__, decl))


def _describe_type(type: Type) -> str:
    if type.is_primitive:
        return type['subtype']

    nullable = ''
    if type.is_nullable: nullable = '?'

    if type.kind == 'identifier':
        return type['identifier'] + nullable

    if type.kind == 'request':
        return 'request<{}>{}'.format(type['subtype'], nullable)

    if type.kind == 'handle':
        subtype = type['subtype']
        if subtype == 'handle':
            return 'handle' + nullable
        else:
            return 'handle<{}>{}'.format(subtype, nullable)

    if type.kind == 'identifier':
        return type['subtype'] + nullable

    element_count = ''
    if 'maybe_element_count' in type:
        element_count = ':{}'.format(type['maybe_element_count'])

    if type.kind == 'string':
        return 'string' + element_count + nullable

    if type.kind == 'vector':
        return 'vector<{}>{}{}'.format(
            _describe_type(type.element_type), element_count, nullable)

    if type.kind == 'array':
        return 'array<{}>{}'.format(
            _describe_type(type.element_type), element_count)

    raise Exception("Don't know how to describe type {!r}".format(type))


def _describe_decl_type(decl: Optional[Declaration]) -> str:
    assert decl is not None
    assert isinstance(decl, (StructMember, ))
    return _describe_type(decl.type)


abi_nonchanges = (StructMemberRenamed, DeclAdded, TableMemberAdded,
                  TableMemberRenamed, TableMemberReserved, TableMemberUnreserved)


def abi_changes(changes: List[Change]) -> Iterator[ClassifiedChange]:
    for change in changes:
        ### General Changes
        if isinstance(change, abi_nonchanges):
            # these changes never break ABI at all
            continue

        if isinstance(change, DeclRemoved):
            decl = change.before
            assert decl is not None
            if isinstance(decl, (Struct, Protocol, Method)):
                yield ClassifiedChange(
                    change, False,
                    '{} removed, make sure there are no remaining users'.
                    format(_describe_decl(decl)))
            else:
                raise Exception(
                    'DeclRemoved for unexpected decl {!r}'.format(decl))

        ### Struct Changes
        elif isinstance(change, StructSizeChanged):
            yield ClassifiedChange(change, True, '{} changed size'.format(
                _describe_struct(change.after)))
        elif isinstance(change, StructMemberRemoved):
            yield ClassifiedChange(change, True, '{} removed'.format(
                _describe_decl(change.before)))
        elif isinstance(change, StructMemberAdded):
            yield ClassifiedChange(change, True, '{} added'.format(
                _describe_decl(change.after)))
        elif isinstance(change, StructMemberSizeChanged):
            yield ClassifiedChange(change, True, '{} changed size'.format(
                _describe_decl(change.after)))
        elif isinstance(change, StructMemberMoved):
            yield ClassifiedChange(change, True, '{} moved'.format(
                _describe_decl(change.after)))
        elif isinstance(change, StructMemberTypeChanged):
            yield ClassifiedChange(change, not change.compatible,
                                   '{} changed type from {} to {}'.format(
                                       _describe_decl(change.after),
                                       _describe_decl_type(change.before),
                                       _describe_decl_type(change.after)))
        elif isinstance(change, StructMemberSplit):
            yield ClassifiedChange(
                change, False,
                '{} split into {} which may or may not be fine'.format(
                    _describe_decl(change.before), ', '.join(
                        m.name for m in change.afters)))
        elif isinstance(change, StructMemberJoined):
            after_field = change.after
            assert after_field is not None
            assert isinstance(after_field, StructMember)
            yield ClassifiedChange(
                change, False,
                '{} members {} joined into {} which may or may not be fine'.
                format(
                    _describe_decl(change.before), ', '.join(
                        m.name for m in change.befores), after_field.name))

        ### Table Changes
        elif isinstance(change, TableMemberRemoved):
            yield ClassifiedChange(
                change, False,
                "{} removed, it should have been marked as reserved".format(
                    _describe_decl(change.before)))

        ### Protocol Changes
        elif isinstance(change, MethodOrdinalChanged):
            yield ClassifiedChange(change, True, '{} ordinal changed'.format(
                _describe_decl(change.before)))
        elif isinstance(change, MethodBecameEvent):
            yield ClassifiedChange(change, True, '{} became event'.format(
                _describe_decl(change.before)))
        elif isinstance(change, EventBecameMethod):
            yield ClassifiedChange(change, True, '{} became method'.format(
                _describe_decl(change.before)))
        elif isinstance(change, MethodGainedResponse):
            yield ClassifiedChange(change, True, '{} gained response'.format(
                _describe_decl(change.before)))
        elif isinstance(change, MethodLostResponse):
            yield ClassifiedChange(change, True, '{} lost response'.format(
                _describe_decl(change.before)))

        else:
            raise Exception(
                "Don't know if change is ABI breaking {!r}".format(change))
