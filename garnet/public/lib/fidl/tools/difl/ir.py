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
    'PrimitiveType',
    'NullableType',
    'HandleType',
    'RequestType',
    'ArrayType',
    'VectorType',
    'StringType',
    'IdentifierType',
    'EnumIdentifierType',
    'BitsIdentifierType',
    'StructIdentifierType',
    'ProtocolIdentifierType',
    'TableIdentifierType',
    'UnionIdentifierType',
    'XUnionIdentifierType',
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


def construct_type(library: 'Library', value: dict) -> 'Type':
    if value['kind'] == 'primitive':
        return PrimitiveType(library, value)
    elif value['kind'] == 'handle':
        return HandleType(library, value)
    elif value['kind'] == 'request':
        return RequestType(library, value)
    elif value['kind'] == 'array':
        return ArrayType(library, value)
    elif value['kind'] == 'vector':
        return VectorType(library, value)
    elif value['kind'] == 'string':
        return StringType(library, value)
    elif value['kind'] == 'identifier':
        return _construct_identifier_type(library, value)
    else:
        raise Exception('Unknown type: %r' % value)


class Type(dict):
    def __init__(self, library: 'Library', value: dict, inline_size: int):
        self.library = library
        self.inline_size = inline_size
        dict.__init__(self, value)

    @property
    def kind(self):
        return self['kind']


class PrimitiveType(Type):
    @staticmethod
    def from_subtype(subtype: str, library: 'Library'):
        return PrimitiveType(library, {
            'kind': 'primitive',
            'subtype': subtype,
        })

    def __init__(self, library: 'Library', value: dict):
        assert value['kind'] == 'primitive'
        if isinstance(value['subtype'], dict):
            # TODO: why do we get this here?
            value = value['subtype']
        subtype = value['subtype']
        if subtype in {'bool', 'int8', 'uint8'}:
            inline_size = 1
        elif subtype in {'int16', 'uint16'}:
            inline_size = 2
        elif subtype in {'int32', 'uint32', 'float32'}:
            inline_size = 4
        elif subtype in {'int64', 'uint64', 'float64'}:
            inline_size = 8
        else:
            raise Exception('Unknown primitive subtype %r' % subtype)
        super().__init__(library, value, inline_size)
        self.subtype = subtype
        self.is_float = self.subtype in ('float32', 'float64')
        self.is_signed = self.subtype in ('int8', 'int16', 'int32',
                                          'int64') or self.is_float


class NullableType(Type):
    def __init__(self, library: 'Library', value: dict, inline_size: int):
        assert 'nullable' in value
        super().__init__(library, value, inline_size)
        self.is_nullable = self['nullable']


class HandleType(NullableType):
    def __init__(self, library: 'Library', value: dict):
        assert value['kind'] == 'handle'
        super().__init__(library, value, 4)
        self.handle_type = self['subtype']


class RequestType(NullableType):
    def __init__(self, library: 'Library', value: dict):
        assert value['kind'] == 'request'
        super().__init__(library, value, 4)
        self.protocol = self['subtype']


class ArrayType(Type):
    def __init__(self, library: 'Library', value: dict):
        assert value['kind'] == 'array'
        self.element_type = construct_type(library, value['element_type'])
        super().__init__(
            library, value,
            value['element_count'] * self.element_type.inline_size)
        self.count: int = self['element_count']


class VectorType(NullableType):
    def __init__(self, library: 'Library', value: dict):
        assert value['kind'] == 'vector'
        super().__init__(library, value, 16)
        self.element_type = construct_type(library, value['element_type'])
        self.limit: t.Optional[int] = self.get('maybe_element_count')


class StringType(NullableType):
    def __init__(self, library: 'Library', value: dict):
        assert value['kind'] == 'string'
        super().__init__(library, value, 16)
        self.element_type = PrimitiveType.from_subtype('uint8', library)
        self.limit: t.Optional[int] = self.get('maybe_element_count')


def _construct_identifier_type(library: 'Library',
                               value: dict) -> 'IdentifierType':
    assert value['kind'] == 'identifier'
    declaration = library.libraries.find(value['identifier'])
    if isinstance(declaration, Enum):
        return EnumIdentifierType(library, value, declaration)
    if isinstance(declaration, Bits):
        return BitsIdentifierType(library, value, declaration)
    if isinstance(declaration, Struct):
        return StructIdentifierType(library, value, declaration)
    if isinstance(declaration, Protocol):
        return ProtocolIdentifierType(library, value, declaration)
    if isinstance(declaration, Table):
        return TableIdentifierType(library, value, declaration)
    if isinstance(declaration, Union):
        return UnionIdentifierType(library, value, declaration)
    if isinstance(declaration, XUnion):
        return XUnionIdentifierType(library, value, declaration)
    raise Exception("Can't construct identifier type %s for %r" %
                    (value['identifier'], declaration))


DECL = t.TypeVar('DECL', bound=Declaration)


class IdentifierType(NullableType):
    def __init__(self, library: 'Library', value: dict, declaration: DECL):
        assert value['kind'] == 'identifier'
        inline_size = 8
        if not value['nullable']:
            inline_size = declaration.inline_size
        super().__init__(library, value, inline_size)
        self.identifier = self['identifier']


class EnumIdentifierType(IdentifierType):
    def __init__(self, library: 'Library', value: dict, declararation: 'Enum'):
        super().__init__(library, value, declararation)
        self.primitive = declararation.type
        self.declaration = declararation


class BitsIdentifierType(IdentifierType):
    def __init__(self, library: 'Library', value: dict, declararation: 'Bits'):
        super().__init__(library, value, declararation)
        self.primitive = declararation.type
        self.declaration = declararation


class StructIdentifierType(IdentifierType):
    def __init__(self, library: 'Library', value: dict,
                 declararation: 'Struct'):
        super().__init__(library, value, declararation)
        self.declaration = declararation


class ProtocolIdentifierType(IdentifierType):
    def __init__(self, library: 'Library', value: dict,
                 declararation: 'Protocol'):
        super().__init__(library, value, declararation)
        self.declaration = declararation


class TableIdentifierType(IdentifierType):
    def __init__(self, library: 'Library', value: dict,
                 declararation: 'Table'):
        super().__init__(library, value, declararation)
        assert not self.is_nullable
        self.declaration = declararation


class UnionIdentifierType(IdentifierType):
    def __init__(self, library: 'Library', value: dict,
                 declararation: 'Union'):
        super().__init__(library, value, declararation)
        self.declaration = declararation


class XUnionIdentifierType(IdentifierType):
    def __init__(self, library: 'Library', value: dict,
                 declararation: 'XUnion'):
        super().__init__(library, value, declararation)
        self.declaration = declararation


class Const(Declaration):
    @property
    def type(self) -> Type:
        return construct_type(self.library, self['type'])


class EnumMember(Declaration):
    def __init__(self, enum: 'Enum', value: dict):
        Declaration.__init__(self, enum.library, value)
        self.enum = enum

    @property
    def value(self) -> int:
        literal = self['value']
        assert literal['kind'] == 'literal'
        assert literal['literal']['kind'] == 'numeric'
        return int(literal['literal']['value'])


class Enum(Declaration):
    @property
    def type(self) -> PrimitiveType:
        return PrimitiveType.from_subtype(self['type'], self.library)

    @property
    def inline_size(self) -> int:
        return self.type.inline_size

    @property
    def members(self) -> t.List[EnumMember]:
        return [EnumMember(self, m) for m in self['members']]


class BitsMember(Declaration):
    def __init__(self, bits: 'Bits', value: dict):
        Declaration.__init__(self, bits.library, value)
        self.bits = bits

    @property
    def value(self) -> int:
        literal = self['value']
        assert literal['kind'] == 'literal'
        assert literal['literal']['kind'] == 'numeric'
        return int(literal['literal']['value'])


class Bits(Declaration):
    def __init__(self, library: 'Library', value: dict):
        if 'location' not in value:
            # TODO: fix fidlc to include bits location
            value['location'] = {'filename': 'nowhere', 'line': 42}
        super().__init__(library, value)
        self.mask = self['mask']

    @property
    def type(self) -> PrimitiveType:
        return PrimitiveType.from_subtype(self['type'], self.library)

    @property
    def inline_size(self) -> int:
        return self.type.inline_size

    @property
    def members(self) -> t.List[BitsMember]:
        return [BitsMember(self, m) for m in self['members']]


class StructMember(Declaration):
    def __init__(self, struct: 'Struct', value: dict):
        Declaration.__init__(self, struct.library, value)
        self.struct = struct
        self.offset: int = self['offset']
        self.size: int = self['size']

    @property
    def type(self) -> Type:
        return construct_type(self.library, self['type'])


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
        return construct_type(self.library, self['type'])


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
        return 4  # it's a handle


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
        return construct_type(self.library, self['type'])


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
        return construct_type(self.library, self['type'])


class Union(Declaration):
    @property
    def members(self) -> t.List[UnionMember]:
        return [UnionMember(self, m) for m in self['members']]


class XUnionMember(Declaration):
    def __init__(self, xunion: 'XUnion', value):
        Declaration.__init__(self, xunion.library, value)
        self.xunion = xunion
        self.ordinal = self['ordinal']

    @property
    def type(self) -> Type:
        return construct_type(self.library, self['type'])


class XUnion(Declaration):
    @property
    def members(self) -> t.List[XUnionMember]:
        return [XUnionMember(self, m) for m in self['members']]


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
        self.bits = self._collect_declarations('bits_declarations', Bits)
        self.protocols = self._collect_declarations('interface_declarations',
                                                    Protocol)
        self.structs = self._collect_declarations('struct_declarations',
                                                  Struct)
        self.tables = self._collect_declarations('table_declarations', Table)
        self.unions = self._collect_declarations('union_declarations', Union)
        self.xunions = self._collect_declarations('xunion_declarations',
                                                  XUnion)

    def _collect_declarations(
            self, key: str,
            ctor: t.Callable[['Library', dict], D]) -> t.List[D]:
        decls: t.List[D] = []
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
             ) -> t.Union[Const, Enum, Protocol, Struct, Table, Union]:
        library_name, _ = identifier.split('/')
        if library_name not in self.by_name:
            raise Exception('Identifier %s not found' % identifier)
        library = self.by_name[library_name]
        return library.find(identifier)
