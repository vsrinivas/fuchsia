#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys
import argparse
import json
import os
import re
from urllib import urlopen


MODULAR_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..',))


def dep_path(name):
  return os.path.join(MODULAR_ROOT, ('%s_VERSION' % name).upper())


def read_dep(name):
  return open(dep_path(name)).read().strip()


def write_dep(name, value):
  open(dep_path(name), 'w').write(str(value))


class ModularDependencies(object):
  """Holds a set of modular dependency versions."""

  def __init__(self, flutter, mojo_sdk, mojo_devtools, mojo):
    self.flutter = flutter
    self.mojo_sdk = mojo_sdk
    self.mojo_devtools = mojo_devtools
    self.mojo = mojo

  def print_with_heading(self, name):
    print '%s versions:' % name
    print 'Flutter:       %s' % self.flutter
    print 'Mojo SDK:      %s' % self.mojo_sdk
    print 'Mojo Devtools: %s' % self.mojo_devtools
    print 'Mojo:          %s' % self.mojo

  @classmethod
  def read(cls):
    return cls(flutter=read_dep('flutter'),
               mojo_sdk=read_dep('mojo_sdk'),
               mojo_devtools=read_dep('mojo_devtools'),
               mojo=read_dep('mojo'))

  def write(self):
    write_dep('flutter', self.flutter)
    write_dep('mojo_sdk', self.mojo_sdk)
    write_dep('mojo_devtools', self.mojo_devtools)
    write_dep('mojo', self.mojo)


def github_get(repo, path, revision='master'):
  url = 'https://raw.githubusercontent.com/%s/%s%s' % (repo, revision, path)
  return urlopen(url).read()


def parse_deps(source, keys):
  # awful, simple hack.
  versions = {}
  for key in keys:
    m = re.search(r"'%s': '([a-f0-9]{40})'" % key, source)
    if m is None:
      print '%s not found' % key
    else:
      versions[key] = m.groups()[0]
  return versions


def update_deps(source, versions):
  for key in versions.keys():
    source = re.sub(r"'%s': '[a-f0-9]{40}'" % key,
                    "'%s': '%s'" % (key, versions[key]),
                    source,
                    count=1)
  return source


def github_revision_for_branch(repo, branch):
  url = 'https://api.github.com/repos/%s/git/refs/%s' % (repo, branch)
  return json.load(urlopen(url))['object']['sha']


def github_commit_message(repo, revision):
  url = 'https://api.github.com/repos/%s/git/commits/%s' % (repo, revision)
  return json.load(urlopen(url))['message']


def resolve_ref(repo, arg):
  if len(arg) == 40:
    # looks enough like a SHA
    return arg
  try:
    return github_revision_for_branch(repo, 'heads/%s' % arg)
  except KeyError:
    print "Couldn't find branch %s in github.com/%s" % (arg, repo)
    sys.exit(1)


def main():
  parser = argparse.ArgumentParser(
      description='Update DEPS based on particular Flutter and Mojo Devtools '
      'revisions')
  parser.add_argument('--flutter', default='alpha', metavar='REV',
                      help='The github.com/flutter/flutter branch or commit to '
                      'use')
  parser.add_argument('--mojo-devtools', default='master', metavar='REV',
                      help='The github.com/domokit/devtools branch or commit '
                      'to use')
  args = parser.parse_args()

  flutter_revision = resolve_ref('flutter/flutter', args.flutter)
  mojo_devtools_revision = resolve_ref('domokit/devtools', args.mojo_devtools)

  # look up flutter engine revision for flutter revision
  flutter_engine_revision = github_get('flutter/flutter',
                                       '/bin/cache/engine.version',
                                       flutter_revision).strip()
  # look up mojo sdk revision for flutter engine revision
  mojo_sdk_revision = parse_deps(github_get('flutter/engine', '/DEPS',
                                            flutter_engine_revision),
                                 ['mojo_sdk_revision'])['mojo_sdk_revision']

  # find the mojo revision that maps to the mojo sdk revision
  mojo_sdk_commit = github_commit_message('domokit/mojo_sdk', mojo_sdk_revision)
  m = re.search('Cr-Mirrored-Commit: ([a-f0-9]{40})', mojo_sdk_commit)
  if m is None:
    print "Couldn't find Cr-Mirrored-Commit in:"
    print mojo_sdk_commit
    sys.exit(1)
  mojo_revision = m.groups()[0]

  # build new dependency object
  new_dependencies = ModularDependencies(flutter=flutter_revision,
                                         mojo_sdk=mojo_sdk_revision,
                                         mojo_devtools=mojo_devtools_revision,
                                         mojo=mojo_revision)

  # load the current dependencies
  current_dependencies = ModularDependencies.read()

  current_dependencies.print_with_heading('Current')
  new_dependencies.print_with_heading('New')

  # update the dependencies
  new_dependencies.write()


if __name__ == '__main__':
  main()

# vim: ts=2:sw=2:tw=80:et:
