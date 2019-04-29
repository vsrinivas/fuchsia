# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import sys
import typing as t

__all__ = [
    'Declaration',
    'Type',
    'Const',
    'EnumMember',
    'Enum',
    'StructMember',
    'Struct',
    'Argument',
    'Method',
    'Protocol',
    'TableMember',
    'Table',
    'UnionMember',
    'Union',
    'XUnion',
    'Library',
    'Libraries',
]

class Declaration(dict):
    def __init__(self, library: 'Library', value: dict):
        dict.__init__(self, value)
        self.library = library

    @property
    def inline_size(self) -> int:
        return self['size']

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
    def inline_size(self) -> int:
        if self.is_primitive:
            if self['subtype'] in {'bool', 'int8', 'uint8'}: return 1
            if self['subtype'] in {'int16', 'uint16'}: return 2
            if self['subtype'] in {'int32', 'uint32', 'float32'}: return 4
            if self['subtype'] in {'int64', 'uint64', 'float64'}: return 8
            raise Exception('Unknown primitive subtype %r' % self['subtype'])
        if self.kind == 'array':
            return self['element_count'] * self.element_type.inline_size
        if self.kind == 'vector' or self.kind == 'string':
            return 16
        if self.kind == 'handle' or self.kind == 'request':
            return 4

        assert self.kind == 'identifier'
        if self.is_nullable:
            return 8

        declaration = self.library.libraries.find(self['identifier'])
        return declaration.inline_size

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
    def inline_size(self) -> int:
        return self.type.inline_size

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

    @property
    def inline_size(self) -> int:
        return 4 # it's a handle


class TableMember(Declaration):
    def __init__(self, table: 'Table', value: dict):
        Declaration.__init__(self, table.library, value)
        self.table = table

    @property
    def name(self) -> str:
        return self.get('name', '')

    @property
    def ordinal(self) -> int:
        return int(self['ordinal'])

    @property
    def reserved(self) -> bool:
        return self['reserved']

    @property
    def type(self) -> Type:
        if self.reserved:
            raise Exception('Reserved table members have no type')
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

class XUnion(Declaration):
    pass


DECLARATION_TYPES = {
    'const': ('const_declarations', Const),
    'enum': ('enum_declarations', Enum),
    'protocol': ('interface_declarations', Protocol),
    'struct': ('struct_declarations', Struct),
    'table': ('table_declarations', Table),
    'union': ('union_declarations', Union),
    'xunion': ('xunion_declarations', XUnion),
}

D = t.TypeVar('D', bound=Declaration)

class Library(dict):
    def __init__(self, libraries: 'Libraries', path: str):

        self.libraries = libraries
        self._path = path
        dict.__init__(self, json.load(open(path)))
        self.name = self['name']

        self.declarations: t.Dict[str, Declaration] = {}
        self.filenames: t.Set[str] = set()
        self.consts = self._collect_declarations('const_declarations', Const)
        self.enums = self._collect_declarations('enum_declarations', Enum)
        self.protocols = self._collect_declarations('interface_declarations', Protocol)
        self.structs = self._collect_declarations('struct_declarations', Struct)
        self.tables = self._collect_declarations('table_declarations', Table)
        self.unions = self._collect_declarations('union_declarations', Union)

    def _collect_declarations(self, key: str, ctor: t.Callable[['Library', dict], D]) -> t.List[D]:
        decls : t.List[D] = []
        for json in self[key]:
            decl: D = ctor(self, json)
            decls.append(decl)
            self.declarations[decl.name] = decl
            self.filenames.add(decl.filename)
        return decls

    def __repr__(self):
        return 'Library<%s>' % self.name

    def find(self, identifier: str) -> Declaration:
        return self.declarations[identifier]

    @property
    def declaration_order(self) -> t.List[str]:
        return list(self['declarations'].keys())


class Libraries(list):
    def __init__(self):
        self.by_name = dict()

    def load_all(self, list_path: str):
        for relative_path in (line.strip()
                              for line in open(list_path).readlines()):
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
