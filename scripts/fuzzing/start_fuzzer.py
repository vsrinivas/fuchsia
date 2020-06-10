#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import subprocess
import sys

from lib.args import ArgParser
from lib.factory import Factory


def main():
    factory = Factory()
    parser = ArgParser(
        factory.cli,
        'Starts the named fuzzer.  Additional arguments are passed through.')
    args = parser.parse()

    cli = factory.cli
    fuzzer = factory.create_fuzzer(args)
    if not args.monitor:
        cli.echo(
            '\n****************************************************************'
        )
        cli.echo(' Starting {}.'.format(fuzzer))
        cli.echo(' Outputs will be written to:')
        cli.echo('   ' + fuzzer.output)
        if not args.foreground:
            cli.echo(' You should be notified when the fuzzer stops.')
            cli.echo(
                ' To check its progress, use "fx fuzz check {}".'.format(
                    fuzzer))
            cli.echo(
                ' To stop it manually, use "fx fuzz stop {}".'.format(fuzzer))
        cli.echo(
            '****************************************************************\n'
        )
        fuzzer.start()
        if not args.foreground:
            subprocess.Popen(['python', sys.argv[0], '--monitor', str(fuzzer)])
    else:
        fuzzer.monitor()
        cli.echo(str(fuzzer) + ' has stopped.')
        cli.echo('Output written to  {}.'.format(fuzzer.output))


if __name__ == '__main__':
    sys.exit(main())
