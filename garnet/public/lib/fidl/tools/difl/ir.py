# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import sys
import typing as t


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

    @property
    def location(self) -> str:
        return '%s:%d' % (self['location']['filename'],
                          self['location']['line'])

    @property
    def filename(self) -> str:
        return self['location']['filename']

    @property
    def line(self) -> int:
        return self['location']['line']


class Type(dict):
    def __init__(self, library, value):
        self.library = library
        dict.__init__(self, value)

    @property
    def kind(self):
        return self['kind']

    @property
    def is_primitive(self) -> bool:
        return self.kind == 'primitive'

    @property
    def is_nullable(self) -> bool:
        return self.get('nullable', False)

    @property
    def is_handle(self) -> bool:
        if self.kind == 'handle' or self.kind == 'request':
            return True
        if self.kind == 'identifier':
            return isinstance(self.library.find(self['identifier']), Protocol)
        return False

    @property
    def element_type(self):
        if 'element_type' in self:
            return Type(self.library, self['element_type'])


class Const(Declaration):
    @property
    def type(self) -> Type:
        return Type(self.library, self['type'])


class EnumMember(Declaration):
    def __init__(self, enum: 'Enum', value: dict):
        Declaration.__init__(self, enum.library, value)
        self.enum = enum


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


class StructMember(Declaration):
    def __init__(self, struct: 'Struct', value: dict):
        Declaration.__init__(self, struct.library, value)
        self.struct = struct
        self.offset: int = self['offset']
        self.size: int = self['size']

    @property
    def type(self) -> Type:
        return Type(self.library, self['type'])


class Struct(Declaration):
    def __init__(self,
                 library: 'Library',
                 value: dict,
                 method: t.Optional['Method'] = None,
                 request: t.Optional[bool] = None):
        super().__init__(library, value)
        self.method = method
        self.request = request
        self.size: int = self['size']
        self.members = [StructMember(self, m) for m in self['members']]


class Argument(Declaration):
    def __init__(self, method: 'Method', value: dict):
        Declaration.__init__(self, method.library, value)
        self.method = method

    @property
    def type(self) -> Type:
        return Type(self.library, self['type'])


class Method(Declaration):
    def __init__(self, protocol: 'Protocol', value: dict):
        Declaration.__init__(self, protocol.library, value)
        self.protocol = protocol

    @property
    def name(self) -> str:
        return '%s.%s' % (self.protocol.name, self['name'])

    def is_event(self) -> bool:
        return 'maybe_request' not in self

    def is_one_way(self) -> bool:
        return 'maybe_response' not in self

    @property
    def has_request(self) -> bool:
        return 'maybe_request' in self

    def request(self) -> t.Optional[Struct]:
        if self['has_request']:
            return self.arguments_struct('Request', self['maybe_request'],
                                         self['maybe_request_size'])
        else:
            return None

    def response(self) -> t.Optional[Struct]:
        if self['has_response']:
            return self.arguments_struct('Response', self['maybe_response'],
                                         self['maybe_response_size'])
        else:
            return None

    def arguments_struct(self, suffix: str, members: t.List,
                         size: int) -> Struct:
        return Struct(
            self.library, {
                'name': '%s:%s' % (self.name, suffix),
                'members': members,
                'size': size,
                'location': self['location'],
            })

    @property
    def ordinal(self) -> int:
        return int(self['ordinal'])


class Protocol(Declaration):
    @property
    def methods(self) -> t.List[Method]:
        return [Method(self, m) for m in self.get('methods', [])]


class TableMember(Declaration):
    def __init__(self, table: 'Table', value: dict):
        Declaration.__init__(self, table.library, value)
        self.table = table

    @property
    def name(self) -> str:
        return self.get('name', '')

    @property
    def reserved(self) -> bool:
        return self['reserved']

    @property
    def type(self) -> Type:
        return Type(self.library, self['type'])


class Table(Declaration):
    @property
    def members(self) -> t.List[TableMember]:
        return [TableMember(self, m) for m in self['members']]


class UnionMember(Declaration):
    def __init__(self, union: 'Union', value):
        Declaration.__init__(self, union.library, value)
        self.union = union

    @property
    def type(self) -> Type:
        return Type(self.library, self['type'])


class Union(Declaration):
    @property
    def members(self) -> t.List[UnionMember]:
        return [UnionMember(self, m) for m in self['members']]


DECLARATION_TYPES = {
    'const': ('const_declarations', Const),
    'enum': ('enum_declarations', Enum),
    'protocol': ('interface_declarations', Protocol),
    'struct': ('struct_declarations', Struct),
    'table': ('table_declarations', Table),
    'union': ('union_declarations', Union),
}


class Library(dict):
    def __init__(self, libraries: 'Libraries', path: str):
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
    def protocols(self) -> t.List[Protocol]:
        return [Protocol(self, v) for v in self['interface_declarations']]

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
            method for protocol in self.protocols
            for method in protocol.methods
        ]

    def find(self, identifier: str
             ) -> t.Union[None, Const, Enum, Protocol, Struct, Table, Union]:
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

    @property
    def declarations(self) -> t.List[Declaration]:
        decls: t.List[Declaration] = []
        decls.extend(self.consts)
        decls.extend(self.enums)
        decls.extend(self.protocols)
        decls.extend(self.structs)
        decls.extend(self.tables)
        decls.extend(self.unions)
        return decls

    @property
    def filenames(self) -> t.List[str]:
        return list(set(decl.filename for decl in self.declarations))


class Libraries(list):
    def __init__(self):
        self.by_name = dict()

    def load_all(self, list_path: str):
        for relative_path in (line.strip() for line in open(list_path).readlines()):
            path = os.path.join(os.path.dirname(list_path), relative_path)
            self.load(path)

    def load(self, path: str) -> Library:
        library = Library(self, path)
        self.append(library)
        self.by_name[library.name] = library

        return library

    @property
    def consts(self) -> t.List[Const]:
        return [const for library in self for const in library.consts]

    @property
    def enums(self) -> t.List[Enum]:
        return [enum for library in self for enum in library.enums]

    @property
    def protocols(self) -> t.List[Protocol]:
        return [protocol for library in self for protocol in library.protocols]

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
             ) -> t.Union[None, Const, Enum, Protocol, Struct, Table, Union]:
        library_name, _ = identifier.split('/')
        if library_name not in self.by_name:
            return None
        library = self.by_name[library_name]
        return library.find(identifier)
