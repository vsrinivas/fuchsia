# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import mojo_bindings.descriptor as _descriptor
import mojo_bindings.reflection as _reflection

import fidl_types_fidl

class FidlFile(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('file_name', _descriptor.TYPE_STRING, 0, 0),
      _descriptor.SingleFieldGroup('specified_file_name', _descriptor.TYPE_NULLABLE_STRING, 1, 0),
      _descriptor.SingleFieldGroup('module_namespace', _descriptor.TYPE_NULLABLE_STRING, 2, 0),
      _descriptor.SingleFieldGroup('attributes', _descriptor.GenericArrayType(_descriptor.StructType(lambda: fidl_types_fidl.Attribute), nullable=True), 3, 0),
      _descriptor.SingleFieldGroup('imports', _descriptor.GenericArrayType(_descriptor.TYPE_STRING, nullable=True), 4, 0),
      _descriptor.SingleFieldGroup('declared_fidl_objects', _descriptor.StructType(lambda: KeysByType), 5, 0),
      _descriptor.SingleFieldGroup('serialized_runtime_type_info', _descriptor.TYPE_NULLABLE_STRING, 6, 0),
      _descriptor.SingleFieldGroup('comments', _descriptor.StructType(lambda: fidl_types_fidl.Comments, nullable=True), 7, 0),
    ],
  }

class FidlFileGraph(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('files', _descriptor.MapType(_descriptor.TYPE_STRING, _descriptor.StructType(lambda: FidlFile)), 0, 0),
      _descriptor.SingleFieldGroup('resolved_types', _descriptor.MapType(_descriptor.TYPE_STRING, _descriptor.UnionType(lambda: fidl_types_fidl.UserDefinedType)), 1, 0),
      _descriptor.SingleFieldGroup('resolved_constants', _descriptor.MapType(_descriptor.TYPE_STRING, _descriptor.StructType(lambda: fidl_types_fidl.DeclaredConstant)), 2, 0),
    ],
  }

class KeysByType(object):
  __metaclass__ = _reflection.MojoStructType
  DESCRIPTOR = {
    'fields': [
      _descriptor.SingleFieldGroup('interfaces', _descriptor.GenericArrayType(_descriptor.TYPE_STRING, nullable=True), 0, 0),
      _descriptor.SingleFieldGroup('structs', _descriptor.GenericArrayType(_descriptor.TYPE_STRING, nullable=True), 1, 0),
      _descriptor.SingleFieldGroup('unions', _descriptor.GenericArrayType(_descriptor.TYPE_STRING, nullable=True), 2, 0),
      _descriptor.SingleFieldGroup('top_level_enums', _descriptor.GenericArrayType(_descriptor.TYPE_STRING, nullable=True), 3, 0),
      _descriptor.SingleFieldGroup('embedded_enums', _descriptor.GenericArrayType(_descriptor.TYPE_STRING, nullable=True), 4, 0),
      _descriptor.SingleFieldGroup('top_level_constants', _descriptor.GenericArrayType(_descriptor.TYPE_STRING, nullable=True), 5, 0),
      _descriptor.SingleFieldGroup('embedded_constants', _descriptor.GenericArrayType(_descriptor.TYPE_STRING, nullable=True), 6, 0),
    ],
  }

