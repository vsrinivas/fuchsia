#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import subprocess
import sys

from lib.args import ArgParser
from lib.cipd import Cipd
from lib.corpus import Corpus
from lib.device import Device
from lib.fuzzer import Fuzzer
from lib.host import Host


def main():
    parser = ArgParser(
        'Starts the named fuzzer.  Additional arguments are passed through.')
    args, libfuzzer_opts, libfuzzer_args, subprocess_args = parser.parse()

    host = Host.from_build()
    device = Device.from_host(host)
    fuzzer = Fuzzer.from_args(device, args)
    fuzzer.libfuzzer_opts = libfuzzer_opts
    fuzzer.libfuzzer_args = libfuzzer_args
    fuzzer.subprocess_args = subprocess_args

    if not args.monitor:
        with Corpus.from_args(fuzzer, args) as corpus:
            cipd = Cipd(corpus)
            if not args.no_cipd:
                cipd.install('latest')
            corpus.push()

        print(
            '\n****************************************************************'
        )
        print(' Starting ' + str(fuzzer) + '.')
        print(' Outputs will be written to:')
        print('   ' + fuzzer.output)
        if not args.foreground:
            print(' You should be notified when the fuzzer stops.')
            print(
                ' To check its progress, use `fx fuzz check ' + str(fuzzer) +
                '`.')
            print(
                ' To stop it manually, use `fx fuzz stop ' + str(fuzzer) + '`.')
        print(
            '****************************************************************\n'
        )
        fuzzer.start()
        if not args.foreground:
            subprocess.Popen(['python', sys.argv[0], '--monitor', str(fuzzer)])
    else:
        fuzzer.monitor()
        title = str(fuzzer) + ' has stopped.'
        body = 'Output written to ' + fuzzer.output + '.'
        print(title)
        print(body)
    return 0


if __name__ == '__main__':
    sys.exit(main())
