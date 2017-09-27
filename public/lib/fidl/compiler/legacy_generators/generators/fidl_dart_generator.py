# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generates dart source files from a mojom.Module."""

import errno
import os
import re
import shutil
import sys

import mojom.fileutil as fileutil
import mojom.generate.constant_resolver as resolver
import mojom.generate.generator as generator
import mojom.generate.module as mojom
import mojom.generate.pack as pack
from mojom.generate.template_expander import UseJinja

GENERATOR_PREFIX = 'dart'

# CAUTION: To generate Dart-style names, and to avoid generating reserved words
# for identifiers, the template files should generate names using
# {{element|name}}, not {{element.name}}.

# Dart reserved words from:
# http://www.ecma-international.org/publications/files/ECMA-ST/ECMA-408.pdf
# We must not generate reserved words for identifiers.
# NB: async, await, and yield are not technically reserved words, but since
# they are not valid identifiers in all contexts, we include them here as well.
_dart_reserved_words = [
  "assert",
  "async",
  "await",
  "break",
  "case",
  "catch",
  "class",
  "const",
  "continue",
  "default",
  "do",
  "else",
  "enum",
  "extends",
  "false",
  "final",
  "finally",
  "for",
  "if",
  "in",
  "is",
  "new",
  "null",
  "rethrow",
  "return",
  "super",
  "switch",
  "this",
  "throw",
  "true",
  "try",
  "var",
  "void",
  "while",
  "with",
  "yield",
]

# These names are the class fields and methods of the Proxy class in
# lib/src/proxy.dart of Dart's mojo package. If these names appear in a .mojom
# they must be mangled to avoid name conflicts. They are mangled by appending
# an underscore ('_'), which is banned on names in mojom interfaces.
_reserved_words = _dart_reserved_words + [
  "close",
  "ctrl",
  "impl",
  "serviceName",
]

_kind_to_dart_default_value = {
  mojom.BOOL:                  "false",
  mojom.INT8:                  "0",
  mojom.UINT8:                 "0",
  mojom.INT16:                 "0",
  mojom.UINT16:                "0",
  mojom.INT32:                 "0",
  mojom.UINT32:                "0",
  mojom.FLOAT:                 "0.0",
  mojom.HANDLE:                "null",
  mojom.DCPIPE:                "null",
  mojom.DPPIPE:                "null",
  mojom.CHANNEL:               "null",
  mojom.VMO:                   "null",
  mojom.PROCESS:               "null",
  mojom.THREAD:                "null",
  mojom.EVENT:                 "null",
  mojom.PORT:                  "null",
  mojom.JOB:                   "null",
  mojom.SOCKET:                "null",
  mojom.EVENTPAIR:             "null",
  mojom.NULLABLE_HANDLE:       "null",
  mojom.NULLABLE_DCPIPE:       "null",
  mojom.NULLABLE_DPPIPE:       "null",
  mojom.NULLABLE_CHANNEL:      "null",
  mojom.NULLABLE_VMO:          "null",
  mojom.NULLABLE_PROCESS:      "null",
  mojom.NULLABLE_THREAD:       "null",
  mojom.NULLABLE_EVENT:        "null",
  mojom.NULLABLE_PORT:         "null",
  mojom.NULLABLE_JOB:          "null",
  mojom.NULLABLE_SOCKET:       "null",
  mojom.NULLABLE_EVENTPAIR:    "null",
  mojom.INT64:                 "0",
  mojom.UINT64:                "0",
  mojom.DOUBLE:                "0.0",
  mojom.STRING:                "null",
  mojom.NULLABLE_STRING:       "null"
}

_kind_to_dart_decl_type = {
  mojom.BOOL:                  "bool",
  mojom.INT8:                  "int",
  mojom.UINT8:                 "int",
  mojom.INT16:                 "int",
  mojom.UINT16:                "int",
  mojom.INT32:                 "int",
  mojom.UINT32:                "int",
  mojom.FLOAT:                 "double",
  mojom.HANDLE:                "zircon.Handle",
  mojom.DCPIPE:                "zircon.Handle",
  mojom.DPPIPE:                "zircon.Handle",
  mojom.CHANNEL:               "zircon.Channel",
  mojom.VMO:                   "zircon.Vmo",
  mojom.PROCESS:               "zircon.Handle",
  mojom.THREAD:                "zircon.Handle",
  mojom.EVENT:                 "zircon.Handle",
  mojom.PORT:                  "zircon.Handle",
  mojom.JOB:                   "zircon.Handle",
  mojom.SOCKET:                "zircon.Socket",
  mojom.EVENTPAIR:             "zircon.Handle",
  mojom.NULLABLE_HANDLE:       "zircon.Handle",
  mojom.NULLABLE_DCPIPE:       "zircon.Handle",
  mojom.NULLABLE_DPPIPE:       "zircon.Handle",
  mojom.NULLABLE_CHANNEL:      "zircon.Channel",
  mojom.NULLABLE_VMO:          "zircon.Vmo",
  mojom.NULLABLE_PROCESS:      "zircon.Handle",
  mojom.NULLABLE_THREAD:       "zircon.Handle",
  mojom.NULLABLE_EVENT:        "zircon.Handle",
  mojom.NULLABLE_PORT:         "zircon.Handle",
  mojom.NULLABLE_JOB:          "zircon.Handle",
  mojom.NULLABLE_SOCKET:       "zircon.Socket",
  mojom.NULLABLE_EVENTPAIR:    "zircon.Handle",
  mojom.INT64:                 "int",
  mojom.UINT64:                "int",
  mojom.DOUBLE:                "double",
  mojom.STRING:                "String",
  mojom.NULLABLE_STRING:       "String"
}

_spec_to_decode_method = {
  mojom.BOOL.spec:                  'decodeBool',
  mojom.DCPIPE.spec:                'decodeHandle',
  mojom.DOUBLE.spec:                'decodeDouble',
  mojom.DPPIPE.spec:                'decodeHandle',
  mojom.FLOAT.spec:                 'decodeFloat',
  mojom.HANDLE.spec:                'decodeHandle',
  mojom.INT16.spec:                 'decodeInt16',
  mojom.INT32.spec:                 'decodeInt32',
  mojom.INT64.spec:                 'decodeInt64',
  mojom.INT8.spec:                  'decodeInt8',
  mojom.CHANNEL.spec:               'decodeChannel',
  mojom.NULLABLE_DCPIPE.spec:       'decodeHandle',
  mojom.NULLABLE_DPPIPE.spec:       'decodeHandle',
  mojom.NULLABLE_HANDLE.spec:       'decodeHandle',
  mojom.NULLABLE_CHANNEL.spec:      'decodeChannel',
  mojom.NULLABLE_VMO.spec:          'decodeVmo',
  mojom.NULLABLE_PROCESS.spec:      'decodeHandle',
  mojom.NULLABLE_THREAD.spec:       'decodeHandle',
  mojom.NULLABLE_EVENT.spec:        'decodeHandle',
  mojom.NULLABLE_PORT.spec:         'decodeHandle',
  mojom.NULLABLE_JOB.spec:          'decodeHandle',
  mojom.NULLABLE_SOCKET.spec:       'decodeSocket',
  mojom.NULLABLE_EVENTPAIR.spec:    'decodeHandle',
  mojom.NULLABLE_STRING.spec:       'decodeString',
  mojom.VMO.spec:                   'decodeVmo',
  mojom.PROCESS.spec:               'decodeHandle',
  mojom.THREAD.spec:                'decodeHandle',
  mojom.EVENT.spec:                 'decodeHandle',
  mojom.PORT.spec:                  'decodeHandle',
  mojom.JOB.spec:                   'decodeHandle',
  mojom.SOCKET.spec:                'decodeSocket',
  mojom.EVENTPAIR.spec:             'decodeHandle',
  mojom.STRING.spec:                'decodeString',
  mojom.UINT16.spec:                'decodeUint16',
  mojom.UINT32.spec:                'decodeUint32',
  mojom.UINT64.spec:                'decodeUint64',
  mojom.UINT8.spec:                 'decodeUint8',
}

_spec_to_encode_method = {
  mojom.BOOL.spec:                  'encodeBool',
  mojom.DCPIPE.spec:                'encodeHandle',
  mojom.DOUBLE.spec:                'encodeDouble',
  mojom.DPPIPE.spec:                'encodeHandle',
  mojom.FLOAT.spec:                 'encodeFloat',
  mojom.HANDLE.spec:                'encodeHandle',
  mojom.INT16.spec:                 'encodeInt16',
  mojom.INT32.spec:                 'encodeInt32',
  mojom.INT64.spec:                 'encodeInt64',
  mojom.INT8.spec:                  'encodeInt8',
  mojom.CHANNEL.spec:               'encodeChannel',
  mojom.NULLABLE_DCPIPE.spec:       'encodeHandle',
  mojom.NULLABLE_DPPIPE.spec:       'encodeHandle',
  mojom.NULLABLE_HANDLE.spec:       'encodeHandle',
  mojom.NULLABLE_CHANNEL.spec:      'encodeChannel',
  mojom.NULLABLE_VMO.spec:          'encodeVmo',
  mojom.NULLABLE_PROCESS.spec:      'encodeHandle',
  mojom.NULLABLE_THREAD.spec:       'encodeHandle',
  mojom.NULLABLE_EVENT.spec:        'encodeHandle',
  mojom.NULLABLE_PORT.spec:         'encodeHandle',
  mojom.NULLABLE_JOB.spec:          'encodeHandle',
  mojom.NULLABLE_SOCKET.spec:       'encodeSocket',
  mojom.NULLABLE_EVENTPAIR.spec:    'encodeHandle',
  mojom.NULLABLE_STRING.spec:       'encodeString',
  mojom.VMO.spec:                   'encodeVmo',
  mojom.PROCESS.spec:               'encodeHandle',
  mojom.THREAD.spec:                'encodeHandle',
  mojom.EVENT.spec:                 'encodeHandle',
  mojom.PORT.spec:                  'encodeHandle',
  mojom.JOB.spec:                   'encodeHandle',
  mojom.SOCKET.spec:                'encodeSocket',
  mojom.EVENTPAIR.spec:             'encodeHandle',
  mojom.STRING.spec:                'encodeString',
  mojom.UINT16.spec:                'encodeUint16',
  mojom.UINT32.spec:                'encodeUint32',
  mojom.UINT64.spec:                'encodeUint64',
  mojom.UINT8.spec:                 'encodeUint8',
}

# The fidl_types.mojom and service_describer.mojom files are special because
# they are used to generate mojom Type's and ServiceDescription implementations.
# They need to be imported, unless the file itself is being generated.
_service_describer_pkg_short = "service_describer"
_fidl_types_pkg_short = "fidl_types"

def GetDartType(kind):
  if kind.imported_from:
    return kind.imported_from["unique_name"] + "." + GetNameForElement(kind)
  return GetNameForElement(kind)

def DartDefaultValue(field):
  if field.default:
    if mojom.IsStructKind(field.kind):
      assert field.default == "default"
      return "new %s()" % GetDartType(field.kind)
    if mojom.IsEnumKind(field.kind):
      return ("new %s(%s)" %
          (GetDartType(field.kind), ExpressionToText(field.default)))
    return ExpressionToText(field.default)
  if field.kind in mojom.PRIMITIVES:
    return _kind_to_dart_default_value[field.kind]
  if mojom.IsStructKind(field.kind):
    return "null"
  if mojom.IsUnionKind(field.kind):
    return "null"
  if mojom.IsArrayKind(field.kind):
    return "null"
  if mojom.IsMapKind(field.kind):
    return "null"
  if mojom.IsInterfaceKind(field.kind) or \
     mojom.IsInterfaceRequestKind(field.kind):
    return "null"
  if mojom.IsEnumKind(field.kind):
    return "null"

def DartDeclType(kind):
  if kind in mojom.PRIMITIVES:
    return _kind_to_dart_decl_type[kind]
  if mojom.IsStructKind(kind):
    return GetDartType(kind)
  if mojom.IsUnionKind(kind):
    return GetDartType(kind)
  if mojom.IsArrayKind(kind):
    array_type = DartDeclType(kind.kind)
    return "List<" + array_type + ">"
  if mojom.IsMapKind(kind):
    key_type = DartDeclType(kind.key_kind)
    value_type = DartDeclType(kind.value_kind)
    return "Map<"+ key_type + ", " + value_type + ">"
  if mojom.IsInterfaceKind(kind):
    return ("bindings.InterfaceHandle<%s>" % GetDartType(kind))
  if mojom.IsInterfaceRequestKind(kind):
    return ("bindings.InterfaceRequest<%s>" % GetDartType(kind.kind))
  if mojom.IsEnumKind(kind):
    return GetDartType(kind)

def NameToComponent(name):
  # insert '_' between anything and a Title name (e.g, HTTPEntry2FooBar ->
  # HTTP_Entry2_FooBar). Numbers terminate a string of lower-case characters.
  name = re.sub('([^_])([A-Z][^A-Z1-9_]+)', r'\1_\2', name)
  # insert '_' between non upper and start of upper blocks (e.g.,
  # HTTP_Entry2_FooBar -> HTTP_Entry2_Foo_Bar).
  name = re.sub('([^A-Z_])([A-Z])', r'\1_\2', name)
  return [x.lower() for x in name.split('_')]

def UpperCamelCase(name):
  return ''.join([x.capitalize() for x in NameToComponent(name)])

def CamelCase(name):
  uccc = UpperCamelCase(name)
  return uccc[0].lower() + uccc[1:]

def DotToUnderscore(name):
    return name.replace('.', '_')

def IsParamStruct(kind):
  assert(isinstance(kind, mojom.Struct))
  return kind.name.endswith('_Params')

# This may generate Dart reserved words. Call GetNameForElement to avoid
# generating reserved words.
def GetNameForElementUnsafe(element):
  if (mojom.IsInterfaceKind(element) or mojom.IsUnionKind(element)):
    return UpperCamelCase(element.name)
  if mojom.IsStructKind(element):
    if (IsParamStruct(element)):
      # Param Structs are library private.
      return '_' + UpperCamelCase(element.name)
    return UpperCamelCase(element.name)
  if mojom.IsInterfaceRequestKind(element):
    return GetNameForElement(element.kind)
  if isinstance(element, (mojom.Constant,
                          mojom.EnumField,
                          mojom.Field,
                          mojom.Method,
                          mojom.NamedValue,
                          mojom.Parameter,
                          mojom.UnionField)):
    return CamelCase(element.name)
  if mojom.IsEnumKind(element):
    # If the enum is nested in some other mojom element, then we
    # mangle the enum name by prepending it with the name of the containing
    # element.
    if element.parent_kind:
      return ("%s%s" % (GetNameForElement(element.parent_kind),
                        UpperCamelCase(element.name)))
    return UpperCamelCase(element.name)
  if isinstance(element, mojom.EnumValue):
    return (GetNameForElement(element.enum) + '.' + CamelCase(element.name))
  raise Exception('Unexpected element: %s' % element)

def GetNameForElement(element):
  name = GetNameForElementUnsafe(element)
  if name in _reserved_words:
    name = name + '_'
  return name

def GetInterfaceResponseName(method):
  return UpperCamelCase(method.name + 'Response')

def GetDartTrueFalse(value):
  return 'true' if value else 'false'

def GetArrayNullabilityFlags(kind):
  """Returns nullability flags for an array type, see codec.dart.

  As we have dedicated decoding functions for arrays, we have to pass
  nullability information about both the array itself, as well as the array
  element type there.
  """
  assert mojom.IsArrayKind(kind)
  ARRAY_NULLABLE   = 'bindings.kArrayNullable'
  ELEMENT_NULLABLE = 'bindings.kElementNullable'
  NOTHING_NULLABLE = 'bindings.kNothingNullable'

  flags_to_set = []
  if mojom.IsNullableKind(kind):
      flags_to_set.append(ARRAY_NULLABLE)
  if mojom.IsNullableKind(kind.kind):
      flags_to_set.append(ELEMENT_NULLABLE)

  if not flags_to_set:
      flags_to_set = [NOTHING_NULLABLE]
  return ' | '.join(flags_to_set)

def AppendDecodeParams(initial_params, kind, bit):
  """ Appends standard parameters for decode calls. """
  params = list(initial_params)
  if (kind == mojom.BOOL):
    params.append(str(bit))
  if mojom.IsReferenceKind(kind):
    if mojom.IsArrayKind(kind):
      params.append(GetArrayNullabilityFlags(kind))
    else:
      params.append(GetDartTrueFalse(mojom.IsNullableKind(kind)))
  if mojom.IsArrayKind(kind):
    params.append(GetArrayExpectedLength(kind))
  return params

def AppendEncodeParams(initial_params, kind, bit):
  """ Appends standard parameters shared between encode and decode calls. """
  params = list(initial_params)
  if (kind == mojom.BOOL):
    params.append(str(bit))
  if mojom.IsReferenceKind(kind):
    if mojom.IsArrayKind(kind):
      params.append(GetArrayNullabilityFlags(kind))
    else:
      params.append(GetDartTrueFalse(mojom.IsNullableKind(kind)))
  if mojom.IsArrayKind(kind):
    params.append(GetArrayExpectedLength(kind))
  return params

def DecodeMethod(kind, offset, bit):
  def _DecodeMethodName(kind):
    if mojom.IsArrayKind(kind):
      return _DecodeMethodName(kind.kind) + 'Array'
    if mojom.IsInterfaceRequestKind(kind):
      return 'decodeInterfaceRequest'
    if mojom.IsInterfaceKind(kind):
      return 'decodeInterfaceHandle'
    return _spec_to_decode_method[kind.spec]
  methodName = _DecodeMethodName(kind)
  params = AppendDecodeParams([ str(offset) ], kind, bit)
  return '%s(%s)' % (methodName, ', '.join(params))

def EncodeMethod(kind, variable, offset, bit):
  def _EncodeMethodName(kind):
    if mojom.IsStructKind(kind):
      return 'encodeStruct'
    if mojom.IsUnionKind(kind):
      return 'encodeUnion'
    if mojom.IsArrayKind(kind):
      return _EncodeMethodName(kind.kind) + 'Array'
    if mojom.IsEnumKind(kind):
      return 'encodeEnum'
    if mojom.IsInterfaceRequestKind(kind):
      return 'encodeInterfaceRequest'
    if mojom.IsInterfaceKind(kind):
      return 'encodeInterfaceHandle'
    return _spec_to_encode_method[kind.spec]
  methodName = _EncodeMethodName(kind)
  params = AppendEncodeParams([ variable, str(offset) ], kind, bit)
  return '%s(%s)' % (methodName, ', '.join(params))

def TranslateConstants(token):
  if isinstance(token, mojom.BuiltinValue):
    if token.value == "double.INFINITY" or token.value == "float.INFINITY":
      return "double.INFINITY";
    if token.value == "double.NEGATIVE_INFINITY" or \
       token.value == "float.NEGATIVE_INFINITY":
      return "double.NEGATIVE_INFINITY";
    if token.value == "double.NAN" or token.value == "float.NAN":
      return "double.NAN";

  # Strip leading '+'.
  if token[0] == '+':
    token = token[1:]

  return token

def ExpressionToText(token):
  if isinstance(token, (mojom.EnumValue, mojom.NamedValue)):
    return str(token.resolved_value)
  return TranslateConstants(token)

def GetArrayKind(kind, size = None):
  if size is None:
    return mojom.Array(kind)
  else:
    array = mojom.Array(kind, 0)
    array.dart_map_size = size
    return array

def GetArrayExpectedLength(kind):
  if mojom.IsArrayKind(kind) and kind.length is not None:
    return getattr(kind, 'dart_map_size', str(kind.length))
  else:
    return 'bindings.kUnspecifiedArrayLength'

def IsPointerArrayKind(kind):
  if not mojom.IsArrayKind(kind):
    return False
  sub_kind = kind.kind
  return mojom.IsObjectKind(sub_kind)

def IsEnumArrayKind(kind):
  return mojom.IsArrayKind(kind) and mojom.IsEnumKind(kind.kind)

def IsImportedKind(kind):
  return hasattr(kind, 'imported_from') and kind.imported_from

def ParseStringAttribute(attribute):
  assert isinstance(attribute, basestring)
  return attribute

# See //build/dart/label_to_package_name.py
# TODO(abarth): Base these paths on the sdk_dirs variable in gn.
_SDK_DIRS = [
  "garnet/public/",
  "peridot/public/",
  "topaz/public/",
]

# Strip the sdk dirs from the given label, if necessary.
def _remove_sdk_dir(label):
  for prefix in _SDK_DIRS:
    if label.startswith(prefix):
      return label[len(prefix):]
  return label

def GetPackage(module):
  if module.path.startswith('/'):
    raise Exception('Uh oh, path %s looks absolute' % module.path)
  return os.path.dirname(_remove_sdk_dir(module.path)).replace('/', '.')

def GetImportUri(module):
  return os.path.join(GetPackage(module), module.name)

def RaiseHelper(msg):
    raise Exception(msg)

class Generator(generator.Generator):

  dart_filters = {
    'array_expected_length': GetArrayExpectedLength,
    'array': GetArrayKind,
    'decode_method': DecodeMethod,
    'default_value': DartDefaultValue,
    'encode_method': EncodeMethod,
    'is_imported_kind': IsImportedKind,
    'is_array_kind': mojom.IsArrayKind,
    'is_map_kind': mojom.IsMapKind,
    'is_numerical_kind': mojom.IsNumericalKind,
    'is_any_handle_kind': mojom.IsAnyHandleKind,
    'is_string_kind': mojom.IsStringKind,
    'is_nullable_kind': mojom.IsNullableKind,
    'is_pointer_array_kind': IsPointerArrayKind,
    'is_enum_array_kind': IsEnumArrayKind,
    'is_struct_kind': mojom.IsStructKind,
    'is_union_kind': mojom.IsUnionKind,
    'is_enum_kind': mojom.IsEnumKind,
    'is_interface_kind': mojom.IsInterfaceKind,
    'is_interface_request_kind': mojom.IsInterfaceRequestKind,
    'dart_true_false': GetDartTrueFalse,
    'dart_type': DartDeclType,
    'name': GetNameForElement,
    'interface_response_name': GetInterfaceResponseName,
    'dot_to_underscore': DotToUnderscore,
    'is_cloneable_kind': mojom.IsCloneableKind,
    'upper_camel': UpperCamelCase,
    'lower_camel': CamelCase,
    'raise': RaiseHelper,
  }

  # If set to True, then mojom type information will be generated.
  should_gen_fidl_types = False

  def GetParameters(self, args):
    package = self.module.name.split('.')[0]

    # True if handles are used anywhere in the mojom.
    has_handles = any(not mojom.IsCloneableKind(kind)
                      for kind in (self.GetStructs() +
                                   self.GetStructsFromMethods() +
                                   self.GetUnions()))

    # True if the binding will need dart:async
    needs_dart_async = any(any(method.response_parameters is not None
                               for method in interface.methods)
                           for interface in self.GetInterfaces())
    service_describer_pkg = "package:lib.fidl.compiler.interfaces/%s.fidl.dart" % \
      _service_describer_pkg_short
    fidl_types_pkg = "package:lib.fidl.compiler.interfaces/%s.fidl.dart" % \
      _fidl_types_pkg_short

    parameters = {
      "namespace": self.module.namespace,
      "imports": self.GetImports(args),
      "kinds": self.module.kinds,
      "enums": self.module.enums,
      "module": resolver.ResolveConstants(self.module, ExpressionToText),
      "structs": self.GetStructs() + self.GetStructsFromMethods(),
      "unions": self.GetUnions(),
      "interfaces": self.GetInterfaces(),
      "imported_interfaces": self.GetImportedInterfaces(),
      "imported_from": self.ImportedFrom(),
      "typepkg": '%s.' % _fidl_types_pkg_short,
      "descpkg": '%s.' % _service_describer_pkg_short,
      "fidl_types_import": 'import \'%s\' as %s;' % \
        (fidl_types_pkg, _fidl_types_pkg_short),
      "service_describer_import": 'import \'%s\' as %s;' % \
        (service_describer_pkg, _service_describer_pkg_short),
      "has_handles": has_handles,
      "needs_dart_async": needs_dart_async,
    }

    # If this is the mojom types package, clear the import-related params.
    if package == _fidl_types_pkg_short:
      parameters["typepkg"] = ""
      parameters["fidl_types_import"] = ""

    # If this is the service describer package, clear the import-related params.
    if package == _service_describer_pkg_short:
      parameters["descpkg"] = ""
      parameters["service_describer_import"] = ""

    # If no interfaces were defined, the service describer import isn't needed.
    if len(self.module.interfaces) == 0:
      parameters["service_describer_import"] = ""

    return parameters

  def GetGlobals(self):
    return {
      'should_gen_fidl_types': self.should_gen_fidl_types,
    }

  @UseJinja("dart_templates/module.lib.tmpl", filters=dart_filters)
  def GenerateLibModule(self, args):
    return self.GetParameters(args)


  def GenerateFiles(self, args):
    self.should_gen_fidl_types = "--generate_type_info" in args

    elements = self.module.namespace.split('.')
    elements.append("%s.dart" % self.module.name)

    lib_module = self.GenerateLibModule(args)

    package_name = GetPackage(self.module)
    gen_path = self.MatchFidlFilePath("%s.dart" % self.module.name)
    self.Write(lib_module, gen_path)

  def GetImports(self, args):
    used_imports = self.GetUsedImports(self.module)
    used_names = set()
    for each_import in used_imports.values():
      simple_name = each_import["module_name"].split(".")[0]

      # Since each import is assigned a library in Dart, they need to have
      # unique names.
      unique_name = simple_name
      counter = 0
      while unique_name in used_names:
        counter += 1
        unique_name = simple_name + str(counter)

      used_names.add(unique_name)
      each_import["unique_name"] = unique_name + '_fidl'
      counter += 1

      each_import["rebased_path"] = GetImportUri(each_import['module'])
    return sorted(used_imports.values(), key=lambda x: x['rebased_path'])

  def GetImportedInterfaces(self):
    interface_to_import = {}
    for each_import in self.module.imports:
      for each_interface in each_import["module"].interfaces:
        name = each_interface.name
        interface_to_import[name] = each_import["unique_name"] + "." + name
    return interface_to_import

  def ImportedFrom(self):
    interface_to_import = {}
    for each_import in self.module.imports:
      for each_interface in each_import["module"].interfaces:
        name = each_interface.name
        interface_to_import[name] = each_import["unique_name"] + "."
    return interface_to_import
