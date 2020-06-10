#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys

from lib.args import ArgParser
from lib.device import Device
from lib.fuzzer import Fuzzer
from lib.host import Host
from lib.cli import CommandLineInterface


def main():
    cli = CommandLineInterface()
    parser = ArgParser(
        cli, 'Reports status for the fuzzer matching NAME if ' +
        'provided, or for all running fuzzers.  Status includes execution ' +
        'state, corpus size, and number of artifacts.')
    parser.require_name(False)
    args = parser.parse_args()

    host = Host.from_build(cli)
    device = Device.from_host(host)
    fuzzers = host.fuzzers(args.name)

    silent = True
    for package, executable in fuzzers:
        fuzzer = Fuzzer(device, package, executable)
        if not args.name and not fuzzer.is_running():
            continue
        silent = False
        if fuzzer.is_running():
            cli.echo(str(fuzzer) + ': RUNNING')
        else:
            cli.echo(str(fuzzer) + ': STOPPED')
        cli.echo('    Output path:  ' + fuzzer.data_path())
        cli.echo(
            '    Corpus size:  %d inputs / %d bytes' % fuzzer.measure_corpus())
        artifacts = fuzzer.list_artifacts()
        if len(artifacts) == 0:
            cli.echo('    Artifacts:    None')
        else:
            cli.echo('    Artifacts:    ' + artifacts[0])
            for artifact in artifacts[1:]:
                cli.echo('                  ' + artifact)
    if silent:
        cli.echo(
            'No fuzzers are running.  Include \'name\' to check specific ' +
            'fuzzers.')


if __name__ == '__main__':
    sys.exit(main())
