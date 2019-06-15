# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import sys
import typing as t


class DeclState(object):
    def __init__(self, indent: str = '', parents: t.Set[str] = frozenset()):
        self.indent = indent
        self.parents = parents

    def nest(self, indent: str = '',
             identifier: t.Optional[str] = None) -> 'DeclState':
        indent = self.indent + indent
        parents = self.parents
        if identifier:
            parents = parents.union({identifier})
        return DeclState(indent, parents)


class Declaration(dict):
    def __init__(self, library: 'Library', value: dict):
        dict.__init__(self, value)
        self.library = library

    @property
    def name(self) -> str:
        return self['name']

    @property
    def attributes(self):
        return dict(
            (a['name'], a['value']) for a in self.get('maybe_attributes', []))


class Type(dict):
    def __init__(self, library, value):
        self.library = library
        dict.__init__(self, value)

    @property
    def kind(self):
        return self['kind']

    def is_primitive(self) -> bool:
        return self.kind == 'primitive'

    def is_nullable(self) -> bool:
        return self.get('nullable', False)

    @property
    def element_type(self):
        if 'element_type' in self:
            return Type(self.library, self['element_type'])

    def decl(self, state) -> str:
        if self.is_primitive():
            return self['subtype']
        nullable = ''
        if self.is_nullable():
            nullable = '?'
        if self.kind == 'identifier':
            if self['identifier'] in state.parents:
                # a cycle
                return self['identifier'] + nullable
            value = self.library.libraries.find(self['identifier'])
            if isinstance(value, Interface):
                return self['identifier'] + nullable
            else:
                return value.decl(
                    state.nest(identifier=self['identifier'])) + nullable
        element_count = self.get('element_count',
                                 self.get('maybe_element_count'))
        size = ''
        if element_count is not None:
            size = ':%d' % element_count
        if self.kind == 'string':
            return 'string' + size + nullable
        if self.kind in ('vector', 'array'):
            return '%s<%s>%s%s' % (
                self.kind,
                Type(self.library, self['element_type']).decl(state), size,
                nullable)
        if self.kind == 'handle':
            if self['subtype'] == 'handle':
                return 'handle'
            else:
                return 'handle<%s>' % self['subtype']
        if self.kind == 'request':
            return 'request<%s>' % self['subtype']
        raise Exception('unknown type %r' % self)


class Const(Declaration):
    @property
    def type(self) -> Type:
        return Type(self.library, self['type'])


class EnumMember(Declaration):
    def __init__(self, enum: 'Enum', value: dict):
        Declaration.__init__(self, enum.library, value)
        self.enum = enum

    def decl(self, state: DeclState) -> str:
        return '%s = %s' % (self.name, self['value']['literal']['value'])


class Enum(Declaration):
    @property
    def type(self) -> Type:
        return Type(self.library, {
            'kind': 'primitive',
            'subtype': self['type']
        })

    @property
    def members(self) -> t.List[EnumMember]:
        return [EnumMember(self, m) for m in self['members']]

    def decl(self, state: DeclState) -> str:
        state = state.nest(identifier=self.name)
        return ('enum %s : %s {' %
                (self.name, self.type.decl(state))) + '\n  ' + state.indent + (
                    ',\n  ' + state.indent).join(
                        m.decl(state)
                        for m in self.members) + '\n' + state.indent + '}'

class Bits(Declaration):
    pass

class Argument(Declaration):
    def __init__(self, method: 'Method', value: dict):
        Declaration.__init__(self, method.library, value)
        self.method = method

    @property
    def type(self) -> Type:
        return Type(self.library, self['type'])

    def decl(self, state: DeclState) -> str:
        return '%s %s' % (self.type.decl(state), self['name'])


class Method(Declaration):
    def __init__(self, interface: 'Interface', value: dict):
        Declaration.__init__(self, interface.library, value)
        self.interface = interface

    @property
    def name(self) -> str:
        return '%s.%s' % (self.interface.name, self['name'])

    def is_event(self) -> bool:
        return 'maybe_request' not in self

    def is_one_way(self) -> bool:
        return 'maybe_response' not in self

    def request(self) -> t.Optional[t.List[Argument]]:
        if self['has_request']:
            return [Argument(self, a) for a in self['maybe_request']]
        else:
            return None

    def response(self) -> t.Optional[t.List[Argument]]:
        if self['has_response']:
            return [Argument(self, a) for a in self['maybe_response']]
        else:
            return None

    def decl(self, state: DeclState) -> str:
        if self.is_event():
            preamble = '%d: -> %s(' % (self['ordinal'], self['name'])
            state = state.nest(indent=' ' * len(preamble))
            return preamble + ', '.join(
                arg.decl(state) for arg in self.response()) + ');'
        if self.is_one_way():
            # TODO
            return 'adsf'

        preamble = '%d: %s(' % (self['ordinal'], self['name'])
        state = state.nest(indent=' ' * len(preamble))

        return (preamble + ', '.join(
            arg.decl(state)
            for arg in self.request()) + ')\n' + state.indent[:-4] + '-> (' +
                ', '.join(arg.decl(state) for arg in self.response()) + ');')


class Interface(Declaration):
    @property
    def methods(self) -> t.List[Method]:
        return [Method(self, m) for m in self.get('methods', [])]


class StructMember(Declaration):
    def __init__(self, struct: 'Struct', value: dict):
        Declaration.__init__(self, struct.library, value)
        self.struct = struct

    @property
    def type(self) -> Type:
        return Type(self.library, self['type'])

    def decl(self, state: DeclState) -> str:
        # TODO: defaults?
        return self.type.decl(state) + ' ' + self.name


class Struct(Declaration):
    @property
    def members(self) -> t.List[StructMember]:
        return [StructMember(self, m) for m in self['members']]

    def decl(self, state: DeclState) -> str:
        sub_state = state.nest(indent='  ', identifier=self.name)
        return ('struct %s {' % self.name) + '\n' + sub_state.indent + (
            ';\n' + sub_state.indent).join(
                m.decl(sub_state)
                for m in self.members) + ';\n' + state.indent + '}'


class TableMember(Declaration):
    def __init__(self, table: 'Table', value: dict):
        Declaration.__init__(self, table.library, value)
        self.table = table

    @property
    def name(self) -> t.Optional[str]:
        return self.get('name')

    @property
    def reserved(self) -> bool:
        return self['reserved']

    @property
    def type(self) -> Type:
        return Type(self.library, self['type'])

    def decl(self, state: DeclState) -> str:
        ordinal = '%d: ' % self['ordinal']
        if self.reserved:
            return ordinal + 'reserved'
        else:
            sub_state = state.nest(indent=' ' * len(ordinal))
            return ordinal + self.type.decl(sub_state) + ' ' + self.name


class Table(Declaration):
    @property
    def members(self) -> t.List[TableMember]:
        return [TableMember(self, m) for m in self['members']]

    def decl(self, state: DeclState) -> str:
        sub_state = state.nest(indent='  ', identifier=self.name)
        return ('table %s {' % self.name) + '\n' + sub_state.indent + (
            ';\n' + sub_state.indent).join(
                m.decl(sub_state)
                for m in self.members) + ';\n' + state.indent + '}'


class UnionMember(Declaration):
    def __init__(self, union: 'Union', value):
        Declaration.__init__(self, union.library, value)
        self.union = union

    @property
    def type(self) -> Type:
        return Type(self.library, self['type'])

    def decl(self, state: DeclState) -> str:
        return self.type.decl(state) + ' ' + self.name


class Union(Declaration):
    @property
    def members(self) -> t.List[UnionMember]:
        return [UnionMember(self, m) for m in self['members']]

    def decl(self, state: DeclState) -> str:
        sub_state = state.nest(indent='  ', identifier=self.name)
        return 'union %s {\n' % self.name + sub_state.indent + (
            ';\n' + sub_state.indent).join(
                m.decl(sub_state)
                for m in self.members) + ';\n' + state.indent + '}'


DECLARATION_TYPES = {
    'const': ('const_declarations', Const),
    'enum': ('enum_declarations', Enum),
    'bits': ('bits_declarations', Bits),
    'interface': ('interface_declarations', Interface),
    'struct': ('struct_declarations', Struct),
    'table': ('table_declarations', Table),
    'union': ('union_declarations', Union),
}


class Library(dict):
    def __init__(self, libraries: 'FidlLibraries', path: str):
        self.libraries = libraries
        self._path = path
        dict.__init__(self, json.load(open(path)))
        self.name = self['name']

    def __repr__(self):
        return 'Library<%s>' % self.name

    @property
    def consts(self) -> t.List[Const]:
        return [Const(self, value) for value in self['const_declarations']]

    @property
    def enums(self) -> t.List[Enum]:
        return [Enum(self, value) for value in self['enum_declarations']]

    @property
    def bits(self) -> t.List[Bits]:
        return [Bits(self, value) for value in self['bits_declarations']]

    @property
    def interfaces(self) -> t.List[Interface]:
        return [Interface(self, v) for v in self['interface_declarations']]

    @property
    def structs(self) -> t.List[Struct]:
        return [Struct(self, value) for value in self['struct_declarations']]

    @property
    def tables(self) -> t.List[Table]:
        return [Table(self, value) for value in self['table_declarations']]

    @property
    def unions(self) -> t.List[Union]:
        return [Union(self, value) for value in self['union_declarations']]

    @property
    def methods(self) -> t.List[Method]:
        return [
            method for interface in self.interfaces
            for method in interface.methods
        ]

    def find(self, identifier: str) -> t.Union[None, Const, Enum, Interface, Struct, Table, Union]:
        if identifier not in self['declarations']:
          return None
        declaration_type = self['declarations'][identifier]
        declarations_key, constructor = DECLARATION_TYPES[declaration_type]
        return self._lookup_declaration(identifier, declarations_key,
                                        constructor)

    def _lookup_declaration(self, identifier, declarations_key, constructor):
        value = next(
            s for s in self[declarations_key] if s['name'] == identifier)
        return constructor(self, value)


class Libraries(list):
    def __init__(self):
        build_dir = os.environ.get('FUCHSIA_BUILD_DIR')
        if build_dir is None:
            print('FUCHSIA_BUILD_DIR is not set.')
            print('Run: fx exec %s' % ' '.join(sys.argv))
            sys.exit(1)

        # find all the .fidl.json files
        ENDING = '.fidl.json'
        for root, _, files in os.walk(build_dir):
            self.extend(
                Library(self, os.path.join(root, f)) for f in files
                if f.endswith(ENDING))
        self.by_name = dict((l.name, l) for l in self)

    @property
    def consts(self) -> t.List[Const]:
        return [const for library in self for const in library.consts]

    @property
    def enums(self) -> t.List[Enum]:
        return [enum for library in self for enum in library.enums]

    @property
    def bits(self) -> t.List[Bits]:
        return [bit for library in self for bit in library.bits]

    @property
    def interfaces(self) -> t.List[Interface]:
        return [
            interface for library in self for interface in library.interfaces
        ]

    @property
    def structs(self) -> t.List[Struct]:
        return [struct for library in self for struct in library.structs]

    @property
    def tables(self) -> t.List[Table]:
        return [table for library in self for table in library.tables]

    @property
    def unions(self) -> t.List[Union]:
        return [union for library in self for union in library.unions]

    @property
    def methods(self) -> t.List[Method]:
        return [method for library in self for method in library.methods]

    def find(self, identifier: str
             ) -> t.Union[None, Const, Enum, Interface, Struct, Table, Union]:
        library_name, _ = identifier.split('/')
        if library_name not in self.by_name:
          return None
        library = self.by_name[library_name]
        return library.find(identifier)
