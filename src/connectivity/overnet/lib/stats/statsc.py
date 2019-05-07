# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import string

# Code generation engine for overnet stats.
#
# Overnet tracks a large number of internal statistics, many on the critical path.
# For this reason, each statistic update is required to be as cheap as possible.
# That said, a large degree of flexibility is needed in handling said statistics.
#
# For these reasons, we generate some code per statistic to ease usage in the
# broader Overnet codebase. Other Python modules import this one to configure that
# code generation.

_CC_PREFIX = """
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "${include}"

namespace overnet {
"""

_CC_COUNTER = "  ${counter} += other.${counter};\n"

_CC_SUFFIX = """
}  // namespace overnet
"""

_H_PREFIX = """
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/connectivity/overnet/lib/stats/visitor.h"
#include <stdint.h>

namespace overnet {

struct ${module}Stats {
  void Merge(const ${module}Stats& other);
  void Accept(StatsVisitor* visitor) const;
"""

_H_SUFFIX = """};

}  // namespace overnet
"""

class Counter:
    def __init__(self, name):
        self.args = { 'counter': name }

    decl = '  uint64_t ${counter} = 0;\n'
    merge = '  ${counter} += other.${counter};\n'
    accept = '  visitor->Counter("${counter}", ${counter});\n'

class Writer:
    def __init__(self, filename, args):
        self.filename = filename
        self.output = []
        self.args = args

    def __enter__(self):
        return self

    def __exit__(self, type, value, traceback):
        with open(self.filename, 'w') as f:
            f.write(''.join(self.output))

    def tpl(self, tpl, **kwargs):
        args = self.args.copy()
        args.update(kwargs)
        self.output.append(string.Template(tpl).substitute(args))

    def foreach(self, pfx, sfx, action, actors):
        self.tpl(pfx)
        for actor in actors:
            self.tpl(getattr(actor, action), **actor.args)
        self.tpl(sfx)

def compile(name, include, stats):
    argp = argparse.ArgumentParser(description='Compile overnet stats description')
    argp.add_argument('--stem')
    args = argp.parse_args()

    module = {
        'module': name,
        'include': include,
    }

    with Writer('%s.cc' % args.stem, module) as out:
        out.tpl(_CC_PREFIX)
        out.foreach(pfx='void ${module}Stats::Merge(const ${module}Stats& other) {\n',
                    sfx='}\n',
                    action='merge',
                    actors=stats)
        out.foreach(pfx='void ${module}Stats::Accept(StatsVisitor* visitor) const {\n',
                    sfx='}\n',
                    action='accept',
                    actors=stats)
        out.tpl(_CC_SUFFIX)

    with Writer('%s.h' % args.stem, module) as out:
        out.tpl(_H_PREFIX)
        out.foreach(pfx='',
                    sfx='', 
                    action='decl',
                    actors=stats)
        out.tpl(_H_SUFFIX)
