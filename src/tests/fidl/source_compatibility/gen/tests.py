# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import re
import unittest

from types_ import *


class TypesTest(unittest.TestCase):

    def test_serialize(self):
        serialized = CompatTest(
            title='Add protocol method',
            fidl={
                'before': FidlDef(source='before.test.fidl', instructions=[]),
                'after': FidlDef(source='after.test.fidl', instructions=[])
            },
            bindings={
                'hlcpp':
                    Steps(
                        starting_fidl='before',
                        starting_src='before.cpp',
                        steps=[
                            SourceStep(
                                step_num=1,
                                source='during.cc',
                                instructions=['do thing']),
                            FidlStep(step_num=2, fidl='after'),
                            SourceStep(
                                step_num=3,
                                source='after.cc',
                                instructions=['voila']),
                        ])
            })
        deserialized = {
            'title': 'Add protocol method',
            'fidl':
                {
                    'before': {
                        'source': 'before.test.fidl',
                        'instructions': []
                    },
                    'after': {
                        'source': 'after.test.fidl',
                        'instructions': []
                    }
                },
            'bindings':
                {
                    'hlcpp':
                        {
                            'starting_fidl':
                                'before',
                            'starting_src':
                                'before.cpp',
                            'steps':
                                [
                                    {
                                        'step_num': 1,
                                        'source': 'during.cc',
                                        'instructions': ['do thing']
                                    },
                                    {
                                        'step_num': 2,
                                        'fidl': 'after',
                                    },
                                    {
                                        'step_num': 3,
                                        'source': 'after.cc',
                                        'instructions': ['voila']
                                    },
                                ]
                        }
                }
        }
        self.assertEquals(serialized.todict(), deserialized)
        self.assertEquals(CompatTest.fromdict(deserialized), serialized)


if __name__ == '__main__':
    unittest.main()
