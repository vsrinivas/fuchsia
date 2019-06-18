# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from difl.ir import *

import typing

__all__ = ['Comparator']


class Comparator:
    def __init__(self):
        self.identifier_shapes_match: typing.Dict[str, bool] = {}
        self.identifier_constraints_match: typing.Dict[str, bool] = {}

        # notice cycles when comparing shapes & contraints
        self.shape_match_stack: typing.List[str] = []
        self.constraint_match_stack: typing.List[str] = []

    def shapes_match(self, before: Type, after: Type) -> bool:
        '''
        Compares two types for shape
        '''
        if isinstance(before, IdentifierType) and \
             isinstance(after, IdentifierType) and \
                 before.identifier == after.identifier and \
                 not before.is_nullable and not before.is_nullable:
            if before.identifier not in self.identifier_shapes_match:
                assert before.identifier not in self.shape_match_stack
                self.shape_match_stack.append(before.identifier)
                self.identifier_shapes_match[
                    before.identifier] = self._shapes_match(before, after)
                assert before.identifier == self.shape_match_stack.pop()
            return self.identifier_shapes_match[before.identifier]
        return self._shapes_match(before, after)

    def _shapes_match(self, before: Type, after: Type) -> bool:
        # identical types are identical
        if before == after and not isinstance(before, IdentifierType):
            return True

        # different sized types are incompatible
        if before.inline_size != after.inline_size:
            return False

        ########## Handles, Protocols and Requests
        # handles are compatible with handles
        if isinstance(before, (ProtocolIdentifierType, RequestType, HandleType)) and \
                 isinstance(after, (ProtocolIdentifierType, RequestType, HandleType)):
            return True

        ########## Primitives
        # compare primitives
        if isinstance(before, PrimitiveType) and \
            isinstance(after, PrimitiveType):
            return before.inline_size == after.inline_size and before.is_float == after.is_float

        ########## Enums and Bits
        # compare enums, bits and integer primitives
        if isinstance(before, (PrimitiveType, EnumIdentifierType, BitsIdentifierType)) and \
                  isinstance(after, (PrimitiveType, EnumIdentifierType, BitsIdentifierType)):
            # get the primitive or underlying type
            b_prim = before if isinstance(before,
                                          PrimitiveType) else before.primitive
            a_prim = after if isinstance(after,
                                         PrimitiveType) else after.primitive
            assert b_prim.inline_size == a_prim.inline_size
            return b_prim.is_float == a_prim.is_float

        ########## Arrays
        if isinstance(before, ArrayType) != isinstance(after, ArrayType):
            # arrays and not-arrays are incompatible
            return False

        if isinstance(before, ArrayType) and isinstance(after, ArrayType):
            if before.count != after.count:
                # changing the size is incompatible
                return False
            # compatibility is based on the member types
            return self.shapes_match(before.element_type, after.element_type)

        ########## Vectors and Strings
        if isinstance(before, (VectorType, StringType)) and \
            isinstance(after, (VectorType, StringType)):
            return self.shapes_match(before.element_type, after.element_type)

        ########## Identifiers
        if isinstance(before, IdentifierType) and \
             isinstance(after, IdentifierType):
            if type(before) != type(after):
                # identifier types changing is a different shape
                return False

            if before.identifier != after.identifier:
                # TODO: deal with renames?
                return False

            if isinstance(before, (XUnionIdentifierType, TableIdentifierType)):
                # never a shape change
                return True

            if before.is_nullable or after.is_nullable:
                if before.is_nullable != after.is_nullable:
                    if isinstance(before, XUnionIdentifierType):
                        # Nullability is soft change for xunions
                        return True
                    else:
                        # No other types should have nullability
                        assert isinstance(
                            before,
                            (StructIdentifierType, UnionIdentifierType))
                        # Nullability changes layout for structs and unions
                        return False
                else:
                    # both nullable, no layout change
                    return True
            # both not-nullable

            if isinstance(before, StructIdentifierType) and \
                    isinstance(after, StructIdentifierType):
                # TODO: support shape-compatible struct member changes here? like joins & splits?
                b_members = before.declaration.members
                a_members = after.declaration.members
                if len(b_members) != len(a_members):
                    return False
                if len(b_members) == 0:
                    # all empty structs are the same
                    return True
                return all(
                    self.shapes_match(b.type, a.type)
                    for b, a in zip(b_members, a_members))

            if isinstance(before, UnionIdentifierType) and \
                    isinstance(after, UnionIdentifierType):
                b_union_members = before.declaration.members
                a_union_members = after.declaration.members
                if len(b_union_members) != len(a_union_members):
                    return False
                return all(
                    self.shapes_match(b.type, a.type)
                    for b, a in zip(b_union_members, a_union_members))

        raise NotImplementedError(
            "Don't know how to compare shape for %r (%r) and %r (%r)" %
            (type(before), before, type(after), after))

    def constraints_match(self, before: Type, after: Type) -> bool:
        '''
        Compares two types for constraints
        '''
        if isinstance(before, IdentifierType) and \
             isinstance(after, IdentifierType) and \
                 before.identifier == after.identifier:
            if before.identifier not in self.identifier_constraints_match:
                if before.identifier in self.constraint_match_stack:
                    # hit a cycle
                    return True
                self.constraint_match_stack.append(before.identifier)
                self.identifier_constraints_match[before.identifier] = \
                    self._constraints_match(before, after)
                assert before.identifier == self.constraint_match_stack.pop()
            return self.identifier_constraints_match[before.identifier]
        return self._constraints_match(before, after)

    def _constraints_match(self, before: Type, after: Type) -> bool:
        if not self.shapes_match(before, after):
            # shape is the ultimate constraint
            return False

        if type(before) != type(after):
            # changing the type of the type breaks constraints
            return False

        ########## Primitives
        if isinstance(before, PrimitiveType) and \
            isinstance(after, PrimitiveType):
            return before.subtype == after.subtype

        ########## Strings
        if isinstance(before, StringType) and isinstance(after, StringType):
            return before.limit == after.limit and \
                before.is_nullable == after.is_nullable

        ########## Vectors
        if isinstance(before, VectorType) and isinstance(after, VectorType):
            return before.limit == after.limit and \
                before.is_nullable == after.is_nullable and \
                self.constraints_match(before.element_type, after.element_type)

        ########## Arrays
        if isinstance(before, ArrayType) and isinstance(after, ArrayType):
            assert before.count == after.count
            return self.constraints_match(before.element_type,
                                          after.element_type)

        ########## Handles
        if isinstance(before, HandleType) and isinstance(after, HandleType):
            return before.handle_type == after.handle_type and \
                    before.is_nullable == after.is_nullable

        if isinstance(before, NullableType) and \
            isinstance(after, NullableType):
            # nullability changes are constraints changes
            if before.is_nullable != after.is_nullable:
                return False

        if isinstance(before, RequestType) and isinstance(after, RequestType):
            return before.protocol == after.protocol

        if isinstance(before, ProtocolIdentifierType) and \
            isinstance(after, ProtocolIdentifierType):
            return before.identifier == after.identifier

        if isinstance(before, StructIdentifierType) and \
                isinstance(after, StructIdentifierType):
            b_struct_members = before.declaration.members
            a_struct_members = after.declaration.members
            assert len(b_struct_members) == len(a_struct_members)
            if len(b_struct_members) == 0:
                # all empty structs are the same
                return True
            return all(
                self.constraints_match(b.type, a.type)
                for b, a in zip(b_struct_members, a_struct_members))

        if isinstance(before, TableIdentifierType) and \
                isinstance(after, TableIdentifierType):
            b_table_members: typing.Dict[int, TableMember] = {
                m.ordinal: m
                for m in before.declaration.members
            }
            a_table_members: typing.Dict[int, TableMember] = {
                m.ordinal: m
                for m in after.declaration.members
            }
            for ordinal, b_member in b_table_members.items():
                a_member = a_table_members.get(ordinal)
                if a_member is None:
                    # leaving out an ordinal breaks constraints
                    return False
                if b_member.reserved or a_member.reserved:
                    # changing to/from reserved is fine
                    continue
                if not self.constraints_match(b_member.type, a_member.type):
                    return False
            # it's fine if more members were added to after
            return True

        if isinstance(before, UnionIdentifierType) and \
                isinstance(after, UnionIdentifierType):
            b_union_members = before.declaration.members
            a_union_members = after.declaration.members
            if len(b_union_members) != len(a_union_members):
                return False
            # empty unions are illegal
            assert len(b_union_members) != 0
            return all(
                self.constraints_match(b.type, a.type)
                for b, a in zip(b_union_members, a_union_members))

        if isinstance(before, XUnionIdentifierType) and \
                isinstance(after, XUnionIdentifierType):
            # Note: this is applying a strict-mode interpretation
            b_xunion_members = before.declaration.members
            a_xunion_members = after.declaration.members
            if len(b_xunion_members) != len(a_xunion_members):
                return False
            # empty xunions are illegal
            assert len(b_xunion_members) > 0
            # members by ordinal
            b_members = {m.ordinal: m for m in b_xunion_members}
            a_members = {m.ordinal: m for m in a_xunion_members}
            # they both have the same set of ordinals
            if frozenset(b_members.keys()) != frozenset(a_members.keys()):
                return False
            return all(
                self.constraints_match(b_members[o].type, a_members[o].type)
                for o in b_members.keys())

        if isinstance(before, EnumIdentifierType) and \
            isinstance(after, EnumIdentifierType):
            # this is the strict-mode interpretation of enums
            assert len(before.declaration.members) == \
                len(after.declaration.members)
            before_member_values = set(
                m.value for m in before.declaration.members)
            after_member_values = set(
                m.value for m in after.declaration.members)
            return before_member_values == after_member_values

        if isinstance(before, BitsIdentifierType) and \
            isinstance(after, BitsIdentifierType):
            # this is the strict-mode interpretation of bits
            return before.declaration.mask == after.declaration.mask

        raise NotImplementedError(
            "Don't know how to compare constraints for %r (%r) and %r (%r)" %
            (type(before), before, type(after), after))
