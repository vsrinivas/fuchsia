# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Code shared by the various language-specific code generators."""

from functools import partial
from itertools import chain
import os.path
import re

import module as mojom
import mojom.fileutil as fileutil
import pack

def ExpectedArraySize(kind):
  if mojom.IsArrayKind(kind):
    return kind.length
  return None

def StudlyCapsToCamel(studly):
  return studly[0].lower() + studly[1:]

def CamelCaseToAllCaps(camel_case):
  return '_'.join(
      word for word in re.split(r'([A-Z][^A-Z]+)', camel_case) if word).upper()

def UnderToCamel(under):
  """Converts underscore_separated strings to CamelCase strings."""
  return ''.join(word.capitalize() for word in under.split('_'))

def WriteFile(contents, full_path):
  # Make sure the containing directory exists.
  full_dir = os.path.dirname(full_path)
  fileutil.EnsureDirectoryExists(full_dir)

  # Dump the data to disk.
  with open(full_path, "w+") as f:
    f.write(contents)

class Generator(object):
  # Pass |output_dir| to emit files to disk. Omit |output_dir| to echo all
  # files to stdout.
  def __init__(self, module, output_dir=None):
    self.module = module
    self.output_dir = output_dir

  def GetStructsFromMethods(self):
    result = []
    for interface in self.module.interfaces:
      for method in interface.methods:
        result.append(self._GetStructFromMethod(method))
        if method.response_parameters != None:
          result.append(self._GetResponseStructFromMethod(method))
    return result

  def GetStructs(self):
    return map(partial(self._AddStructComputedData, True), self.module.structs)

  def GetUnions(self):
    return self.module.unions

  def GetInterfaces(self):
    return map(self._AddInterfaceComputedData, self.module.interfaces)

  def GetUsedImports(self, module):
    """GetUsedImports computes the imports that are used in the provided module.

    An import being used means that a type or constant defined in the import is
    referenced in the provided module.

    Args:
      module: {module.Module} The module whose used imports are to be computed.

    Returns:
      {dict<str, dict>} A dictionary of the used imports. The key is the file
      name as defined in the import's Module.path. The value is a dictionary.
      The contents of the dictionary is identical to that found in the
      imported_from field of mojom elements.
    """
    used = {}

    def AddImport(element):
      """AddImport is a utility function that adds the import of the provided
      element to the used dictionary defined above.
      """
      # Only named values or kinds could be imported.
      if (not isinstance(element, mojom.Kind) and
          not isinstance(element, mojom.NamedValue)):
        return

      if mojom.IsArrayKind(element) or mojom.IsInterfaceRequestKind(element):
        AddImport(element.kind)
        return
      if mojom.IsMapKind(element):
        AddImport(element.key_kind)
        AddImport(element.value_kind)
        return
      if not hasattr(element, 'imported_from') or not element.imported_from:
        return

      imported_from = element.imported_from
      used[imported_from['module'].path] = imported_from

    # We want to collect the structs that represent method input and output
    # parameters.
    all_structs = list(module.structs)
    for interface in module.interfaces:
      for method in interface.methods:
        all_structs.append(self._GetStructFromMethod(method))
        if method.response_parameters:
          all_structs.append(self._GetResponseStructFromMethod(method))

    for struct in all_structs:
      for field in struct.fields:
        AddImport(field.kind)
        if field.default:
          AddImport(field.default)

    # Enums can be defined in the module, in structs or in interfaces.
    enum_containers = [module] + module.structs + module.interfaces
    enums = [c.enums for c in enum_containers]
    for enum in chain.from_iterable(enums):
      for field in enum.fields:
        if field.value:
          AddImport(field.value)

    for union in module.unions:
      for field in union.fields:
        AddImport(field.kind)

    for constant in module.constants:
      AddImport(constant.value)

    return used

  # Prepend the filename with a directory that matches the directory of the
  # original .mojom file, relative to the import root.
  def MatchMojomFilePath(self, filename):
    return os.path.join(os.path.dirname(self.module.path), filename)

  def Write(self, contents, filename):
    if self.output_dir is None:
      print contents
      return
    full_path = os.path.join(self.output_dir, filename)
    WriteFile(contents, full_path)

  def GenerateFiles(self, args):
    raise NotImplementedError("Subclasses must override/implement this method")

  def GetJinjaParameters(self):
    """Returns default constructor parameters for the jinja environment."""
    return {}

  def GetGlobals(self):
    """Returns global mappings for the template generation."""
    return {}

  def _AddStructComputedData(self, exported, struct):
    """Adds computed data to the given struct. The data is computed once and
    used repeatedly in the generation process."""
    if not hasattr(struct, 'packed') or struct.packed is None:
      struct.packed = pack.PackedStruct(struct)
      struct.bytes = pack.GetByteLayout(struct.packed)
    struct.exported = exported
    return struct

  def _AddInterfaceComputedData(self, interface):
    """Adds computed data to the given interface. The data is computed once and
    used repeatedly in the generation process."""
    for method in interface.methods:
      method.param_struct = self._GetStructFromMethod(method)
      if method.response_parameters is not None:
        method.response_param_struct = self._GetResponseStructFromMethod(method)
      else:
        method.response_param_struct = None
    return interface

  def _GetStructFromMethod(self, method):
    """Returns a method's parameters as a struct."""
    return self._AddStructComputedData(False, method.param_struct)

  def _GetResponseStructFromMethod(self, method):
    """Returns a method's response_parameters as a struct."""
    return self._AddStructComputedData(False, method.response_param_struct)
