#! /usr/bin/python

# Copyright 2016 The Fuchsia Authors. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#    * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#    * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Creates GN build files for Fuchsia/BoringSSL.

This module generates a number of build files not found in 'vanilla' BoringSSL
that are needed to compile, link, and package the command line bssl tool and the
BoringSSL unit tests, as well as provide the GN targets for libcrypto and libssl
for use by other Fuchsia packages.
"""

from collections import OrderedDict
import json
import os
import re
import subprocess
import sys

# Enum for types of files that can be located with find_files().
SOURCE, UNIT_TEST, TEST_SOURCE  = range(3)

class FuchsiaBuilder(object):
  """FuchsiaBuilder generates test related GN build files.

  FuchsiaBuilder outputs GN build files for Fuchsia/BoringSSL consistent with
  the current source tree and with 'util/all_test.json'
  """
  def __init__(self):
    self._tests = None
    self._gtests = []
    fuchsia_dir = os.getenv('FUCHSIA_DIR')
    if fuchsia_dir == None:
      print 'Failed to get FUCHSIA_DIR; have you sourced env.sh?'
    else:
      self.fuchsia_dir = os.path.realpath(fuchsia_dir)
    self.bssl_dir = os.path.join(self.fuchsia_dir, 'third_party', 'boringssl')
    self.build_dir = os.path.join(self.fuchsia_dir, 'build', 'secondary', 'third_party', 'boringssl')

  def find_files(self, start, file_set, skip = None):
    """Locates files under a directory.

    Walks a directory tree and accumulates files that match a given FileSet.  A
    subdirectory named 'test' is always skipped; files from such a directory can
    only by returned by starting with it as the root of the directory tree.

    Args:
      start: The start of directory tree to recursively search.
      file_set: A type of file to locate. See FileSet for values.

    Returns:
      A list of paths to matching files, relative to self.bssl_dir.
    """
    exts_re = re.compile(r'\.c$|\.cc$')
    test_re = re.compile(r'_test|^example_')
    files = []
    for (path, dirnames, filenames) in os.walk(os.path.join(self.bssl_dir, start)):
      dirnames[:] = [dirname for dirname in dirnames if dirname != 'test']
      for filename in filenames:
        if filename == 'gtest_main.cc':
          continue
        if exts_re.search(filename) is None:
          continue
        if not start.endswith('test') and (test_re.search(filename) is not None) != (file_set is UNIT_TEST):
          continue
        pathname = os.path.relpath(os.path.join(path, filename), self.bssl_dir)
        if skip and pathname in skip:
          continue
        if pathname in files:
          continue
        files.append(pathname)
    return files

  def generate_code(self, workdir, stem):
    """Generates source files.

    Iterates through the list of generated source files and invokes a generator
    for each.
    """
    cwd = os.path.join(self.bssl_dir, workdir)
    out = os.path.join(cwd, '%s.c' % stem)
    gen = os.path.join(cwd, '%s_generate.go' % stem)
    with open(out, 'w') as c:
      subprocess.check_call(['go', 'run', gen], cwd=cwd, stdout=c)
    print 'Generated //' + os.path.relpath(out, self.fuchsia_dir)

  def generate_gn(self):
    """Generate the GN build file for Fuchsia/BoringSSL.

    Takes a template and examines it for places to insert list of files as
    specified by the config, and writes out the resulting GN file.

    Args:
      config: Path to a GN template file.
      output: Path to the GN file to output.
    """
    generate_re = re.compile(r'(\s*)#\s*GENERATE\s*(\w+)')
    todo_re = re.compile(r'\s*#\s*TODO')
    build_template = os.path.join(self.build_dir, 'BUILD_template.gn')
    build_gn = os.path.join(self.build_dir, 'BUILD.gn')
    file_path = os.path.relpath(__file__, self.fuchsia_dir)
    with open(build_template, 'r') as template:
      with open(build_gn, 'w') as out:
        for line in template:
          if re.search(todo_re, line):
            continue
          match = re.search(generate_re, line)
          if not match:
            out.write(line)
            continue
          what = match.group(2)
          if what == 'comment':
            out.write('# This file was generated by %s.' % file_path)
            out.write(' Do not edit manually.\n')
          elif what == 'crypto_sources':
            self._generate_sources(out, [ 'crypto' ])
          elif what == 'ssl_sources':
            self._generate_sources(out, [ 'ssl' ])
          elif what == 'bssl_sources':
            self._generate_sources(out, [ 'tool' ])
          elif what == 'test_support_sources':
            self._generate_sources(out, [ 'crypto/test' ])
          elif what == 'unit_tests':
            self._generate_tests(out, [ 'crypto', 'ssl' ])
          elif what == 'crypto_tests':
            self._generate_gtests(out, 'crypto')
          elif what == 'ssl_tests':
            self._generate_gtests(out, 'ssl' )
          else:
            print 'Failed to parse GN template, unknown reference "%s"' % (what)
    gn_bin = os.path.join(self.fuchsia_dir, 'buildtools', 'gn')
    subprocess.call([gn_bin, 'format', build_gn])
    print 'Generated //' + os.path.relpath(build_gn, self.fuchsia_dir)

  def generate_manifest(self, module, odict):
    """Generates a module file that can be passed to '//packages/gn/gen.py'.

    Creates a JSON file that can be consumed by '//packages/gn/gen.py' to
    produce module build files for Ninja.  This JSON file maps GN targets within
    //third_party/boringssl to files to be included in the extra bootfs that can
    be passed when booting magenta.

    Args:
      module: Name of the GN file to output.
      odict:  Dictionary to output as JSON.
    """
    output = os.path.join(self.fuchsia_dir, 'packages', 'gn', module)
    with open(output, 'w') as out:
      json.dump(odict, out, indent=4, separators=(',', ': '))
      out.write('\n')
    print 'Generated //' + os.path.relpath(output, self.fuchsia_dir)

  def generate_modules(self):
    """Generates all module file for BoringSSL'.

    Creates 3 JSON files:
      //packages/gn/boringssl-libcrypto
      //packages/gn/boringssl-libssl
      //packages/gn/boringssl-bssl
    """
    # Create boringssl-libcrypto
    odict = OrderedDict()
    odict['labels'] = ['//third_party/boringssl:crypto']
    odict['binaries'] = [{'binary': 'libcrypto.so',
                          'bootfs_path': 'lib/libcrypto.so'}]
    self.generate_manifest('boringssl_libcrypto', odict)
    # Create boringssl-libssl
    odict = OrderedDict()
    odict['imports'] = ['boringssl_libcrypto', 'netstack']
    odict['labels'] = ['//third_party/boringssl:ssl']
    odict['binaries'] = [{'binary': 'libssl.so',
                          'bootfs_path': 'lib/libssl.so'}]
    self.generate_manifest('boringssl_libssl', odict)
    # Create boringssl
    odict = OrderedDict()
    odict['imports'] = ['boringssl_libcrypto', 'boringssl_libssl']
    odict['labels'] = ['//third_party/boringssl:bssl',
                       '//third_party/boringssl:unit_tests']
    binaries = [{'binary': 'bssl', 'bootfs_path': 'bin/bssl'}]
    for name in self._tests.iterkeys():
      binaries.append({'binary': name, 'bootfs_path': 'test/boringssl/' + name})
    odict['binaries'] = binaries
    resources = []
    path = os.path.relpath(self.bssl_dir, self.fuchsia_dir)
    for deps in self._tests.itervalues():
      for dep in deps[1:]:
        resources.append(
            {'file': '%s/%s' % (path, dep),
             'bootfs_path': 'test/boringssl/data/%s' % os.path.basename(dep)})
    odict['resources'] = resources
    self.generate_manifest('boringssl', odict)

  def _generate_sources(self, out, paths):
    """Generates a source list and writes it out to the GN file.

    Reads lists of directories for each of the file set types except UNIT_TEST
    and calls find_files to build a list of files to insert into the GN file
    being generated.

    Args:
      out: The GN file being generated.
      what: The name of the config item to generate sources for.
    """
    files = []
    for path in paths:
      files += self.find_files(path, SOURCE)
    for file in sorted(files):
      out.write('    "%s",\n' % file)

  def _generate_tests(self, out, paths):
    # First, load the non-gtests from the JSON file and record the data deps.
    tests = {}
    all_tests = os.path.join(self.bssl_dir, 'util', 'all_tests.json')
    with open(all_tests, 'r') as data:
      for test in json.load(data):
        if test[0].startswith('decrepit/'):
          continue
        name = os.path.basename(test[0])
        if name not in tests:
          tests[name] = []
        if len(test) > 1:
          tests[name].append(test[-1])
    # Now search for all unit testss
    self._tests = OrderedDict(sorted(tests.items()))
    names = []
    for path in paths:
      for test in self.find_files(path, UNIT_TEST):
        # gtest-based unit tests handled separately
        if subprocess.call(['grep', '-q', 'gtest/gtest.h', test], cwd = self.bssl_dir) == 0:
          self._gtests.append(test)
          continue
        # All other unit tests are standalone targets.
        name = os.path.splitext(os.path.basename(test))[0]
        if name not in self._tests:
          print 'Warning: %s does not have a corresponding JSON element.' % name
          continue
        self._tests[name].insert(0, test)

        names.append(name)
    names = sorted(names)
    for name in names:
      files = self._tests[name]
      out.write('unit_test("%s") {\n' % name)
      out.write('sources = ["%s",]\n' % files[0])
      if len(files) > 1:
        out.write('data = [')
        for dep in files[1:]:
          out.write('"%s", ' % dep)
        out.write(']\n')
      out.write('}\n\n')
    # Create the overall target
    out.write('group("unit_tests") {\n')
    out.write('testonly = true\n')
    out.write('deps = [')
    for name in names:
      out.write('":%s", ' % name)
    # Include the gtest targets
    out.write('":crypto_test",\n')
    out.write('":ssl_test",\n')
    out.write(']\n')
    out.write('}\n\n')

  def _generate_gtests(self, out, prefix):
    for test in self._gtests:
      if test.startswith(prefix):
        out.write('"%s",\n' % test)

def main():
  """Creates GN build files for Fuchsia/BoringSSL.

  This is the main method of this module, and invokes each of the other
  'generate' and 'write' methods to create the build files needed to compile,
  link, and package the command line bssl tool and the BoringSSL unit tests.

  Returns:
    An integer indicating success (zero) or failure (non-zero).
  """
  fb = FuchsiaBuilder()
  fb.generate_code(os.path.join('crypto', 'err'), 'err_data')
  fb.generate_gn()
  fb.generate_modules()
  print '\nAll BoringSSL/Fuchsia build files generated.'
  return 0


if __name__ == '__main__':
  sys.exit(main())
