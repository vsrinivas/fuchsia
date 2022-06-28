# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import generate_pipeline_test_cml
import os
import tempfile
import unittest

EXPECTED_CML = """{
    include: [ "//src/sys/test_runners/inspect/default.shard.cml" ],
    program: {
        accessor: "ALL",
        timeout_seconds: "60",
        cases: [
          "bootstrap/archivist:root/fuchsia.inspect.Health:status WHERE [s] s == 'OK'",
          "bootstrap/archivist:root/pipelines/feedback:filtering_enabled WHERE [s] s",
          "bootstrap/archivist:root/pipelines/feedback/config_files/* WHERE [s] Count(s) == 2",
          "bootstrap/archivist:root/pipelines/feedback/config_files/archivist","bootstrap/archivist:root/pipelines/feedback/config_files/component_manager"
        ],
    },
}
"""

EXPECTED_DISABLED_CML = """{
    include: [ "//src/sys/test_runners/inspect/default.shard.cml" ],
    program: {
        accessor: "ALL",
        timeout_seconds: "60",
        cases: [
          "bootstrap/archivist:root/fuchsia.inspect.Health:status WHERE [s] s == 'OK'",
          "bootstrap/archivist:root/pipelines/feedback:filtering_enabled WHERE [s] Not(s)",
          "bootstrap/archivist:root/pipelines/feedback/config_files/* WHERE [s] Count(s) == 2",
          "bootstrap/archivist:root/pipelines/feedback/config_files/archivist","bootstrap/archivist:root/pipelines/feedback/config_files/component_manager"
        ],
    },
}
"""


class GeneratePipelineTestCml(unittest.TestCase):

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()

    def testGenerateCml(self):
        out_path = os.path.join(self.temp_dir, 'test.cml')
        generate_pipeline_test_cml.run(
            'feedback', [
                'archivist/inspect/archivist.cfg', 'foo/bar/baz',
                'component_manager/inspect/component_manager.cfg'
            ], out_path, False)
        with open(out_path, 'r') as f:
            self.assertEqual(f.read(), EXPECTED_CML)

    def testGenerateCmlExpectDisabled(self):
        out_path = os.path.join(self.temp_dir, 'test.cml')
        generate_pipeline_test_cml.run(
            'feedback', [
                'archivist/inspect/archivist.cfg', 'foo/bar/baz',
                'component_manager/inspect/component_manager.cfg'
            ], out_path, True)
        with open(out_path, 'r') as f:
            self.assertEqual(f.read(), EXPECTED_DISABLED_CML)


if __name__ == '__main__':
    unittest.main()
