# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import mojo_bindings.descriptor as _descriptor
import mojo_bindings.reflection as _reflection

class SimpleType(object):
  __metaclass__ = _reflection.MojoEnumType
  VALUES = [
    ('BOOL', 0),
    ('DOUBLE', 1),
    ('FLOAT', 2),
    ('INT8', 3),
    ('INT16', 4),
    ('INT32', 5),
    ('INT64', 6),
    ('UINT8', 7),
    ('UINT16', 8),
    ('UINT32', 9),
    ('UINT64', 10),
  ]

class BuiltinConstantValue(object):
  __metaclass__ = _reflection.MojoEnumType
  VALUES = [
    ('DOUBLE_INFINITY', 0),
    ('DOUBLE_NEGATIVE_INFINITY', 1),
    ('DOUBLE_NAN', 2),
    ('FLOAT_INFINITY', 3),
    ('FLOAT_NEGATIVE_INFINITY', 4),
    ('FLOAT_NAN', 5),
  ]

class StringType(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.BooleanGroup([_descriptor.FieldDescriptor('nullable', _descriptor.TYPE_BOOL, 0, 0)]),
    ],
  }

class HandleType(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'enums': {
      'Kind': [
          ('UNSPECIFIED', 0),
          ('MESSAGE_PIPE', 1),
          ('DATA_PIPE_CONSUMER', 2),
          ('DATA_PIPE_PRODUCER', 3),
          ('SHARED_BUFFER', 4),
        ],
    },
    'fields': [
      _descriptor.BooleanGroup([_descriptor.FieldDescriptor('nullable', _descriptor.TYPE_BOOL, 0, 0)]),
      _descriptor.SingleFieldGroup('kind', _descriptor.TYPE_INT32, 1, 0, default_value=0),
    ],
  }

class ArrayType(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.BooleanGroup([_descriptor.FieldDescriptor('nullable', _descriptor.TYPE_BOOL, 0, 0)]),
      _descriptor.SingleFieldGroup('fixed_length', _descriptor.TYPE_INT32, 1, 0, default_value=-1),
      _descriptor.SingleFieldGroup('element_type', _descriptor.UnionType(lambda: Type), 2, 0),
    ],
  }

class MapType(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.BooleanGroup([_descriptor.FieldDescriptor('nullable', _descriptor.TYPE_BOOL, 0, 0)]),
      _descriptor.SingleFieldGroup('key_type', _descriptor.UnionType(lambda: Type), 1, 0),
      _descriptor.SingleFieldGroup('value_type', _descriptor.UnionType(lambda: Type), 2, 0),
    ],
  }

class TypeReference(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.BooleanGroup([_descriptor.FieldDescriptor('nullable', _descriptor.TYPE_BOOL, 0, 0), _descriptor.FieldDescriptor('is_interface_request', _descriptor.TYPE_BOOL, 1, 0)]),
      _descriptor.SingleFieldGroup('identifier', _descriptor.TYPE_NULLABLE_STRING, 2, 0),
      _descriptor.SingleFieldGroup('type_key', _descriptor.TYPE_NULLABLE_STRING, 3, 0),
    ],
  }

class StructField(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('decl_data', _descriptor.StructType(lambda: DeclarationData, nullable=True), 0, 0),
      _descriptor.SingleFieldGroup('type', _descriptor.UnionType(lambda: Type), 1, 0),
      _descriptor.SingleFieldGroup('default_value', _descriptor.UnionType(lambda: DefaultFieldValue, nullable=True), 2, 0),
      _descriptor.SingleFieldGroup('offset', _descriptor.TYPE_UINT32, 3, 0),
      _descriptor.SingleFieldGroup('bit', _descriptor.TYPE_INT8, 4, 0),
      _descriptor.SingleFieldGroup('min_version', _descriptor.TYPE_UINT32, 5, 0),
    ],
  }

class DefaultKeyword(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
  }

class StructVersion(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('version_number', _descriptor.TYPE_UINT32, 0, 0),
      _descriptor.SingleFieldGroup('num_fields', _descriptor.TYPE_UINT32, 1, 0),
      _descriptor.SingleFieldGroup('num_bytes', _descriptor.TYPE_UINT32, 2, 0),
    ],
  }

class MojomStruct(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('decl_data', _descriptor.StructType(lambda: DeclarationData, nullable=True), 0, 0),
      _descriptor.SingleFieldGroup('fields', _descriptor.GenericArrayType(_descriptor.StructType(lambda: StructField)), 1, 0),
      _descriptor.SingleFieldGroup('version_info', _descriptor.GenericArrayType(_descriptor.StructType(lambda: StructVersion), nullable=True), 2, 0),
    ],
  }

class UnionField(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('decl_data', _descriptor.StructType(lambda: DeclarationData, nullable=True), 0, 0),
      _descriptor.SingleFieldGroup('type', _descriptor.UnionType(lambda: Type), 1, 0),
      _descriptor.SingleFieldGroup('tag', _descriptor.TYPE_UINT32, 2, 0),
    ],
  }

class MojomUnion(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('decl_data', _descriptor.StructType(lambda: DeclarationData, nullable=True), 0, 0),
      _descriptor.SingleFieldGroup('fields', _descriptor.GenericArrayType(_descriptor.StructType(lambda: UnionField)), 1, 0),
    ],
  }

class EnumValue(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('decl_data', _descriptor.StructType(lambda: DeclarationData, nullable=True), 0, 0),
      _descriptor.SingleFieldGroup('initializer_value', _descriptor.UnionType(lambda: Value, nullable=True), 1, 0),
      _descriptor.SingleFieldGroup('int_value', _descriptor.TYPE_INT32, 2, 0),
    ],
  }

class MojomEnum(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('decl_data', _descriptor.StructType(lambda: DeclarationData, nullable=True), 0, 0),
      _descriptor.SingleFieldGroup('values', _descriptor.GenericArrayType(_descriptor.StructType(lambda: EnumValue)), 1, 0),
    ],
  }

class MojomMethod(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('decl_data', _descriptor.StructType(lambda: DeclarationData, nullable=True), 0, 0),
      _descriptor.SingleFieldGroup('parameters', _descriptor.StructType(lambda: MojomStruct), 1, 0),
      _descriptor.SingleFieldGroup('response_params', _descriptor.StructType(lambda: MojomStruct, nullable=True), 2, 0),
      _descriptor.SingleFieldGroup('ordinal', _descriptor.TYPE_UINT32, 3, 0),
      _descriptor.SingleFieldGroup('min_version', _descriptor.TYPE_UINT32, 4, 0),
    ],
  }

class MojomInterface(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('decl_data', _descriptor.StructType(lambda: DeclarationData, nullable=True), 0, 0),
      _descriptor.SingleFieldGroup('service_name', _descriptor.TYPE_NULLABLE_STRING, 1, 0),
      _descriptor.SingleFieldGroup('methods', _descriptor.MapType(_descriptor.TYPE_UINT32, _descriptor.StructType(lambda: MojomMethod)), 2, 0),
      _descriptor.SingleFieldGroup('current_version', _descriptor.TYPE_UINT32, 3, 0),
    ],
  }

class ConstantReference(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('identifier', _descriptor.TYPE_STRING, 0, 0),
      _descriptor.SingleFieldGroup('constant_key', _descriptor.TYPE_STRING, 1, 0),
    ],
  }

class EnumValueReference(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('identifier', _descriptor.TYPE_STRING, 0, 0),
      _descriptor.SingleFieldGroup('enum_type_key', _descriptor.TYPE_STRING, 1, 0),
      _descriptor.SingleFieldGroup('enum_value_index', _descriptor.TYPE_UINT32, 2, 0),
    ],
  }

class DeclaredConstant(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('decl_data', _descriptor.StructType(lambda: DeclarationData), 0, 0),
      _descriptor.SingleFieldGroup('type', _descriptor.UnionType(lambda: Type), 1, 0),
      _descriptor.SingleFieldGroup('value', _descriptor.UnionType(lambda: Value), 2, 0),
      _descriptor.SingleFieldGroup('resolved_concrete_value', _descriptor.UnionType(lambda: Value, nullable=True), 3, 0),
    ],
  }

class Attribute(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('key', _descriptor.TYPE_STRING, 0, 0),
      _descriptor.SingleFieldGroup('value', _descriptor.UnionType(lambda: LiteralValue), 1, 0),
    ],
  }

class DeclarationData(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('attributes', _descriptor.GenericArrayType(_descriptor.StructType(lambda: Attribute), nullable=True), 0, 0),
      _descriptor.SingleFieldGroup('short_name', _descriptor.TYPE_NULLABLE_STRING, 1, 0),
      _descriptor.SingleFieldGroup('full_identifier', _descriptor.TYPE_NULLABLE_STRING, 2, 0),
      _descriptor.SingleFieldGroup('declared_ordinal', _descriptor.TYPE_INT32, 3, 0, default_value=-1),
      _descriptor.SingleFieldGroup('declaration_order', _descriptor.TYPE_INT32, 4, 0, default_value=-1),
      _descriptor.SingleFieldGroup('source_file_info', _descriptor.StructType(lambda: SourceFileInfo, nullable=True), 5, 0),
      _descriptor.SingleFieldGroup('contained_declarations', _descriptor.StructType(lambda: ContainedDeclarations, nullable=True), 6, 0),
      _descriptor.SingleFieldGroup('container_type_key', _descriptor.TYPE_NULLABLE_STRING, 7, 0),
      _descriptor.SingleFieldGroup('comments', _descriptor.StructType(lambda: Comments, nullable=True), 8, 0),
    ],
  }

class Comments(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('for_attributes', _descriptor.StructType(lambda: Comments, nullable=True), 0, 0),
      _descriptor.SingleFieldGroup('above', _descriptor.GenericArrayType(_descriptor.TYPE_STRING), 1, 0),
      _descriptor.SingleFieldGroup('left', _descriptor.GenericArrayType(_descriptor.TYPE_STRING), 2, 0),
      _descriptor.SingleFieldGroup('right', _descriptor.GenericArrayType(_descriptor.TYPE_STRING), 3, 0),
      _descriptor.SingleFieldGroup('at_bottom', _descriptor.GenericArrayType(_descriptor.TYPE_STRING), 4, 0),
    ],
  }

class SourceFileInfo(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('file_name', _descriptor.TYPE_STRING, 0, 0),
      _descriptor.SingleFieldGroup('line_number', _descriptor.TYPE_UINT32, 1, 0),
      _descriptor.SingleFieldGroup('column_number', _descriptor.TYPE_UINT32, 2, 0),
    ],
  }

class ContainedDeclarations(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('enums', _descriptor.GenericArrayType(_descriptor.TYPE_STRING, nullable=True), 0, 0),
      _descriptor.SingleFieldGroup('constants', _descriptor.GenericArrayType(_descriptor.TYPE_STRING, nullable=True), 1, 0),
    ],
  }

class RuntimeTypeInfo(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('services', _descriptor.MapType(_descriptor.TYPE_STRING, _descriptor.TYPE_STRING), 0, 0),
      _descriptor.SingleFieldGroup('type_map', _descriptor.MapType(_descriptor.TYPE_STRING, _descriptor.UnionType(lambda: UserDefinedType)), 1, 0),
    ],
  }

class Type(object):
  __metaclass__ = _reflection.MojoUnionType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('simple_type', _descriptor.TYPE_INT32, 0, 0),
      _descriptor.SingleFieldGroup('string_type', _descriptor.StructType(lambda: StringType), 1, 0),
      _descriptor.SingleFieldGroup('array_type', _descriptor.StructType(lambda: ArrayType), 2, 0),
      _descriptor.SingleFieldGroup('map_type', _descriptor.StructType(lambda: MapType), 3, 0),
      _descriptor.SingleFieldGroup('handle_type', _descriptor.StructType(lambda: HandleType), 4, 0),
      _descriptor.SingleFieldGroup('type_reference', _descriptor.StructType(lambda: TypeReference), 5, 0),
    ],
   }

class UserDefinedType(object):
  __metaclass__ = _reflection.MojoUnionType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('enum_type', _descriptor.StructType(lambda: MojomEnum), 0, 0),
      _descriptor.SingleFieldGroup('struct_type', _descriptor.StructType(lambda: MojomStruct), 1, 0),
      _descriptor.SingleFieldGroup('union_type', _descriptor.StructType(lambda: MojomUnion), 2, 0),
      _descriptor.SingleFieldGroup('interface_type', _descriptor.StructType(lambda: MojomInterface), 3, 0),
    ],
   }

class DefaultFieldValue(object):
  __metaclass__ = _reflection.MojoUnionType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('value', _descriptor.UnionType(lambda: Value), 0, 0),
      _descriptor.SingleFieldGroup('default_keyword', _descriptor.StructType(lambda: DefaultKeyword), 1, 0),
    ],
   }

class Value(object):
  __metaclass__ = _reflection.MojoUnionType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('literal_value', _descriptor.UnionType(lambda: LiteralValue), 0, 0),
      _descriptor.SingleFieldGroup('constant_reference', _descriptor.StructType(lambda: ConstantReference), 1, 0),
      _descriptor.SingleFieldGroup('enum_value_reference', _descriptor.StructType(lambda: EnumValueReference), 2, 0),
      _descriptor.SingleFieldGroup('builtin_value', _descriptor.TYPE_INT32, 3, 0),
    ],
   }

class LiteralValue(object):
  __metaclass__ = _reflection.MojoUnionType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('bool_value', _descriptor.TYPE_BOOL, 0, 0),
      _descriptor.SingleFieldGroup('double_value', _descriptor.TYPE_DOUBLE, 1, 0),
      _descriptor.SingleFieldGroup('float_value', _descriptor.TYPE_FLOAT, 2, 0),
      _descriptor.SingleFieldGroup('int8_value', _descriptor.TYPE_INT8, 3, 0),
      _descriptor.SingleFieldGroup('int16_value', _descriptor.TYPE_INT16, 4, 0),
      _descriptor.SingleFieldGroup('int32_value', _descriptor.TYPE_INT32, 5, 0),
      _descriptor.SingleFieldGroup('int64_value', _descriptor.TYPE_INT64, 6, 0),
      _descriptor.SingleFieldGroup('string_value', _descriptor.TYPE_STRING, 7, 0),
      _descriptor.SingleFieldGroup('uint8_value', _descriptor.TYPE_UINT8, 8, 0),
      _descriptor.SingleFieldGroup('uint16_value', _descriptor.TYPE_UINT16, 9, 0),
      _descriptor.SingleFieldGroup('uint32_value', _descriptor.TYPE_UINT32, 10, 0),
      _descriptor.SingleFieldGroup('uint64_value', _descriptor.TYPE_UINT64, 11, 0),
    ],
   }

