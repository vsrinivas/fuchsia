#!/usr/bin/env python
# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script accepts the output of the mojom parser and uses that
# data to invoke the code generators.

import argparse
import imp
import os
import sys


def _ParseCLIArgs():
  """Parses the command line arguments.

  Returns:
    tuple<Namespace, list<str>> The first value of the tuple is a Namespace
    holding the value of the optional args. The second value of the tuple is
    a list of the remaining arguments.
  """
  parser = argparse.ArgumentParser(
      description='Generate bindings from mojom parser output.')
  parser.add_argument('filenames', nargs='*',
                      help='Filter on the set of .mojom files for which code '
                      'will be generated.')
  parser.add_argument('-f', '--file-graph', dest='file_graph',
                      help='Location of the parser output. "-" for stdin. '
                      '(default "-")', default='-')
  parser.add_argument("-o", "--output-dir", dest="output_dir", default=".",
                      help="output directory for generated files")
  parser.add_argument("-g", "--generators", dest="generators_string",
                      metavar="GENERATORS",
                      default="c++,dart,python",
                      help="comma-separated list of generators")
  parser.add_argument("-s", "--src-root-path", dest="src_root_path",
                      default=".",
                      help="relative path to the root of the source tree.")
  parser.add_argument("--no-gen-imports", action="store_true",
                      help="Generate code only for the files that are "
                      "specified on the command line. By default, code "
                      "is generated for all specified files and their "
                      "transitive imports.")
  parser.add_argument("--generate-type-info", dest="generate_type_info",
                      action="store_true",
                      help="generate mojom type descriptors")
  parser.set_defaults(generate_type_info=False)

  return parser.parse_known_args()

# We assume this script is located in the Mojo SDK in tools/bindings.
# If __file__ is a link, we look for the real location of the script.
BINDINGS_DIR = os.path.dirname(os.path.realpath(os.path.abspath(__file__)))
SDK_ROOT = os.path.abspath(os.path.join(BINDINGS_DIR, os.pardir, os.pardir))
PYTHON_SDK_DIR = os.path.abspath(os.path.join(SDK_ROOT, "python"))

sys.path.insert(0, PYTHON_SDK_DIR)
# In order to use fidl_files_fidl we need to make sure the dummy mojo_system
# can be found on the python path.
sys.path.insert(0, os.path.join(PYTHON_SDK_DIR, "dummy_mojo_system"))

sys.path.insert(0, os.path.join(BINDINGS_DIR, "pylib"))


from mojom.generate.generated import fidl_files_fidl
from mojom.generate import mojom_translator
from mojom.parse import parser_runner
from mojo_bindings import serialization


def LoadGenerators(generators_string):
  if not generators_string:
    return []  # No generators.

  generators_dir = os.path.join(BINDINGS_DIR, "generators")
  generators = []
  for generator_name in [s.strip() for s in generators_string.split(",")]:
    generator_name_lower = generator_name.lower()
    # "Built-in" generators:
    if generator_name_lower == "c++":
      generator_py_name = os.path.join(generators_dir,
        "fidl_cpp_generator.py")
    elif generator_name_lower == "dart":
      generator_py_name = os.path.join(generators_dir,
        "fidl_dart_generator.py")
    elif generator_name_lower == "go":
      generator_py_name = os.path.join(generators_dir,
        "fidl_go_generator.py")
    elif generator_name_lower == "python":
      generator_py_name = os.path.join(generators_dir,
        "fidl_python_generator.py")
    # Specified generator python module:
    elif generator_name.endswith(".py"):
      generator_py_name = generator_name
    else:
      print "Unknown generator name %s" % generator_name
      sys.exit(1)
    generator_module = imp.load_source(os.path.basename(generator_py_name)[:-3],
                                       generator_py_name)
    generators.append(generator_module)
  return generators


def ReadFidlFileGraphFromFile(fp):
  """Reads a fidl_files_fidl.FidlFileGraph from a file.

  Args:
    fp: A file pointer from which a serialized mojom_fileS_mojom.FidlFileGraph
        can be read.

  Returns:
    The fidl_files_fidl.FidlFileGraph that was deserialized from the file.
  """
  return parser_runner.DeserializeFidlFileGraph(fp.read())

def FixModulePath(module, abs_src_root_path):
  """Fix the path attribute of the provided module and its imports.

  The path provided for the various modules is the absolute path to the mojom
  file which the module represents. But the generators expect the path to be
  relative to the root of the source tree.

  Args:
    module: {module.Module} whose path is to be updated.
    abs_src_root_path: {str} absolute path to the root of the source tree.
  """

  module.path = os.path.relpath(module.path, abs_src_root_path)
  if not hasattr(module, 'imports'):
    return
  for transitive_import in module.transitive_imports:
    FixModulePath(transitive_import['module'], abs_src_root_path)


def main():
  args, remaining_args = _ParseCLIArgs()

  if args.file_graph == '-':
    fp = sys.stdin
  else:
    fp = open(args.file_graph)

  mojom_file_graph = ReadFidlFileGraphFromFile(fp)
  mojom_modules = mojom_translator.TranslateFileGraph(mojom_file_graph)

  # Note that we are using the word "module" in two unrelated ways here.
  # A mojom module is the Python data structure defined in module.py that
  # represents a Mojom file (sometimes referred to as a Mojom module.)
  # A generator module is a Python module in the sense of the entity the Python
  # runtime loads corresponding to a .py file.
  generator_modules = LoadGenerators(args.generators_string)

  abs_src_root_path = os.path.abspath(args.src_root_path)
  for _, mojom_module in mojom_modules.iteritems():
    # If --no-gen-imports is specified then skip the code generation step for
    # any modules that do not have the |specified_name| field set. This field
    # being set indicates that the module was translated from a .mojom file
    # whose name was explicitly requested during parsing. Otherwise the module
    # is included only becuase of a mojom import statement.
    if args.no_gen_imports and not mojom_module.specified_name:
      continue
    FixModulePath(mojom_module, abs_src_root_path)
    for generator_module in generator_modules:
      generator = generator_module.Generator(mojom_module, args.output_dir)

      # Look at unparsed args for generator-specific args.
      filtered_args = []
      if hasattr(generator_module, 'GENERATOR_PREFIX'):
        prefix = '--' + generator_module.GENERATOR_PREFIX + '_'
        filtered_args = [arg for arg in remaining_args
                         if arg.startswith(prefix)]
      if args.generate_type_info:
        filtered_args.append("--generate_type_info")

      generator.GenerateFiles(filtered_args)


if __name__ == "__main__":
  sys.exit(main())
