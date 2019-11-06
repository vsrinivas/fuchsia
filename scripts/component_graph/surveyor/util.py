#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Utilities to survey and analyze the relationships between components in the system.
"""

import json
import os
import re
from server.graph import ComponentGraphGenerator

"""Utility to dump the component graph in CSV form.
"""
class Surveyor:
  groups_json_path = 'scripts/component_graph/surveyor/groups.json'

  def __init__(self, groups_json):
    """Creates a new Surveyor.
    `groups_json` is a dictionary that maps groups to lists of component names, which it will
    use to label components. Any components in the `Tests` group will be omitted from the results.
    """
    self.groups_json = groups_json

  def export(self, packages, services):
    """Returns a information of all components in `packages` and their dependencies determined by
    the mappings in `services`.
    """
    gen = ComponentGraphGenerator()
    graph = gen.generate(packages, services)
    group_map = Surveyor._gen_group_map(self.groups_json)
    tuples = []
    # Hide tests from the results.
    # TODO: Include and categorize tests?
    test_re = re.compile('.*[_\\-](unit)?test.*\\.cmx')
    for node in graph.nodes.values():
      if node.ty == 'builtin' or node.ty == 'inferred':
        continue
      if test_re.match(node.id):
        continue
      offers = '\\n'.join(node.offers)
      component_deps = '\\n'.join([dep.split('/')[-1] for dep in node.component_use_deps])
      features = '\\n'.join(node.features)
      uses = '\\n'.join(node.uses)
      if node.name in group_map:
        group = group_map[node.name]
      else:
        group = 'Misc'
      if group == 'Tests':
        continue
      tuples.append((group, node.name, offers, component_deps, features, uses))
      tuples.sort(key=lambda tup: tup[0])
    tuples.insert(0, ('Group','Component','Services exposed','Component Deps','Features','Service deps'))
    return tuples

  @classmethod
  def load_groups_json(cls, fuchsia_root):
    """Loads the `groups_json` dictionary from a file."""
    path = os.path.join(fuchsia_root, cls.groups_json_path)
    if os.path.exists(path):
      return json.loads(open(path, 'r').read())
    return {}

  @staticmethod
  def _gen_group_map(groups_json):
    group_map = {}
    for group in groups_json:
      for name in groups_json[group]:
        group_map[name + '.cmx'] = group
    return group_map
