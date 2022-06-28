# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys

CML = """{{
    include: [ "//src/sys/test_runners/inspect/default.shard.cml" ],
    program: {{
        accessor: "ALL",
        timeout_seconds: "60",
        cases: [
          "bootstrap/archivist:root/fuchsia.inspect.Health:status WHERE [s] s == 'OK'",
          "{filtering_enabled_selector}",
          "{count_selector}",
          {selectors}
        ],
    }},
}}
"""

FILTERING_ENABLED_SELECTOR = 'bootstrap/archivist:root/pipelines/{pipeline_name}:filtering_enabled WHERE [s] {value}'

COUNT_SELECTOR = 'bootstrap/archivist:root/pipelines/{pipeline_name}/config_files/* WHERE [s] Count(s) == {count}'

CONFIG_FILE_SELECTOR = 'bootstrap/archivist:root/pipelines/{pipeline_name}/config_files/{file_name}'


def path_to_selector(pipeline_name, path):
    """
    Creates an archivist selector for the given pipeline and selector config file.

    Args:
        pipeline_name: The name of the privacy pipeline.
        path: A path to a config file in that pipeline.

    Returns:
        A selector to verify that config health in the archivist inspect.
    """
    file, ext = os.path.splitext(os.path.basename(path))
    if ext != '.cfg':
        return None
    return CONFIG_FILE_SELECTOR.format(
        pipeline_name=pipeline_name, file_name=file)


def run(pipeline_name, files, output_path, expect_disabled):
    selectors = (path_to_selector(pipeline_name, path) for path in files)
    selectors = [s for s in selectors if s is not None]
    count = len(selectors)
    selectors = ','.join(['"{}"'.format(s) for s in selectors])

    count_selector = COUNT_SELECTOR.format(
        pipeline_name=pipeline_name, count=count)

    value = 'Not(s)' if expect_disabled else 's'
    filtering_enabled_selector = FILTERING_ENABLED_SELECTOR.format(
        pipeline_name=pipeline_name, value=value)

    cml = CML.format(
        selectors=selectors,
        count_selector=count_selector,
        filtering_enabled_selector=filtering_enabled_selector)

    with open(output_path, 'w') as f:
        f.write(cml)


def main():
    parser = argparse.ArgumentParser(
        description=
        'Process the given selector pipeline files into selectors for Inspect.')
    parser.add_argument(
        '-f', '--file', action='append', help='Selector file', default=[])
    parser.add_argument('-n', '--name', help='The pipeline name', required=True)
    parser.add_argument(
        '-o',
        '--out',
        help='The path where the cml will be written',
        required=True)
    parser.add_argument(
        '-d',
        '--expect-disabled',
        action='store_true',
        help='If set, expect the pipeline to be disabled')
    args = parser.parse_args()
    run(args.name, args.file, args.out, args.expect_disabled)


if __name__ == '__main__':
    sys.exit(main())
