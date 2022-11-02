#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Convert the binary Bazel workspace log to text format.'''

import argparse
import os
import platform
import subprocess
import sys

from typing import Optional

_SCRIPT_DIR = os.path.dirname(__file__)
_FUCHSIA_DIR = os.path.abspath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))


def get_host_platform() -> str:
    '''Return host platform name, following Fuchsia conventions.'''
    if sys.platform == 'linux':
        return 'linux'
    elif sys.platform == 'darwin':
        return 'mac'
    else:
        return os.uname().sysname


def get_host_arch() -> str:
    '''Return host CPU architecture, following Fuchsia conventions.'''
    host_arch = os.uname().machine
    if host_arch == 'x86_64':
        return 'x64'
    elif host_arch.startswith(('armv8', 'aarch64')):
        return 'arm64'
    else:
        return host_arch


def get_host_tag():
    '''Return host tag, following Fuchsia conventions.'''
    return '%s-%s' % (get_host_platform(), get_host_arch())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--jre',
        help=
        'Specify Java JRE used to run the parser, default uses the prebuilt Bazel JRE.'
    )
    parser.add_argument(
        '--log-parser-jar',
        help='Specify alternative location for parser .jar file.')
    parser.add_argument(
        '--exclude_rule',
        action="append",
        default=[],
        help="Rule(s) to filter out while parsing.")
    parser.add_argument(
        '--output_path',
        help='Output file location. If not used, output goes to stdout.')
    parser.add_argument('log_file', help='Input log file.')
    args = parser.parse_args()

    # Find the Java Runtime Environment to run the parser first.
    java_binary = os.path.join('bin', 'java')
    if sys.platform.startswith('win'):
        java_binary += '.exe'

    def find_java_binary(jre_path: str) -> Optional[str]:
        path = os.path.join(jre_path, java_binary)
        return path if os.path.exists(path) else None

    if args.jre:
        java_launcher = find_java_binary(args.jre)
        if not java_launcher:
            parser.error('Invalid JRE path: ' + args.jre)
            return 1
    else:
        # Auto-detect the prebuilt bazel JRE first
        prebuilt_bazel_jdk = os.path.join(
            _FUCHSIA_DIR, 'prebuilt', 'third_party', 'bazel', get_host_tag(),
            'install_base', 'embedded_tools', 'jdk')
        java_launcher = find_java_binary(prebuilt_bazel_jdk)
        if not java_launcher:
            print(
                'ERROR: Missing prebuilt Bazel JDK launcher, please use --jre=<DIR>: %s/%s'
                % (prebuilt_bazel_jdk, java_binary),
                file=sys.stderr)
            return 1

    # Find the parser JAR file now.
    if args.log_parser_jar:
        log_parser_jar = args.log_parser_jar
    else:
        log_parser_jar = os.path.join(
            _FUCHSIA_DIR, 'prebuilt', 'third_party', 'bazel_workspacelogparser',
            'bazel_workspacelogparser.jar')
    if not os.path.exists(log_parser_jar):
        parser.error('Missing parser file: ' + log_parser_jar)

    cmd = [java_launcher, '-jar', log_parser_jar, '--log_path=' + args.log_file]
    cmd += ['--exclude_rule=' + rule for rule in args.exclude_rule]
    if args.output_path:
        cmd += ['--output_path=' + args.output_path]

    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
