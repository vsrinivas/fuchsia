#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys
import unittest

from fuzzer import Fuzzer

# This file contains the top-level implementations for each of the subcommands
# for "fx fuzz".


def list_fuzzers(args, factory):
    """Implementation of "fx fuzz list"."""
    host = factory.create_host()
    fuzzers = host.fuzzers(args.name)
    if len(fuzzers) == 0:
        factory.cli.echo('No matching fuzzers.')
    else:
        factory.cli.echo('Found {} matching fuzzers:'.format(len(fuzzers)))
        for package, executable in fuzzers:
            factory.cli.echo('  {}/{}'.format(package, executable))


def start_fuzzer(args, factory):
    """Implementation of "fx fuzz start"."""
    fuzzer = factory.create_fuzzer(args)
    if not args.monitor:
        factory.cli.echo(
            'Starting {}.'.format(fuzzer),
            'Outputs will be written to: {}'.format(fuzzer.output))
        if not args.foreground:
            factory.cli.echo(
                'Check status with "fx fuzz check {}".'.format(fuzzer),
                'Stop manually with "fx fuzz stop {}".'.format(fuzzer))
        fuzzer.start()
        if not args.foreground:
            cmd = ['python', sys.argv[0], 'start', '--monitor']
            if fuzzer.output:
                cmd += ['--output', fuzzer.output]
            cmd.append(str(fuzzer))
            factory.cli.create_process(cmd).popen()
    else:
        fuzzer.monitor()
        factory.cli.echo(
            '{} has stopped.'.format(fuzzer),
            'Output written to: {}.'.format(fuzzer.output))


def check_fuzzer(args, factory):
    """Implementation of "fx fuzz check"."""
    device = factory.create_device()
    status = None
    for package, executable in device.host.fuzzers(args.name):
        fuzzer = Fuzzer(device, package, executable)
        if not args.name and not fuzzer.is_running():
            continue
        status = 'RUNNING' if fuzzer.is_running() else 'STOPPED'
        num, size = fuzzer.corpus.measure()
        artifacts = fuzzer.list_artifacts()
        factory.cli.echo(
            '{}: {}'.format(fuzzer, status),
            '    Output path:  {}'.format(fuzzer.output),
            '    Corpus size:  {} inputs / {} bytes'.format(num, size),
            '    Artifacts:    {}'.format(len(artifacts)))
        for artifact in artifacts:
            factory.cli.echo('        {}'.format(artifact))
    if not status:
        factory.cli.echo(
            'No fuzzers are running.',
            'Include \'name\' to check specific fuzzers.')


def stop_fuzzer(args, factory):
    """Implementation of "fx fuzz stop"."""
    fuzzer = factory.create_fuzzer(args)
    if fuzzer.is_running():
        factory.cli.echo('Stopping {}.'.format(fuzzer))
        fuzzer.stop()
    else:
        factory.cli.echo('{} is already stopped.'.format(fuzzer))


def repro_units(args, factory):
    """Implementation of "fx fuzz repro"."""
    fuzzer = factory.create_fuzzer(args)
    fuzzer.repro()


def analyze_fuzzer(args, factory):
    """Implementation of "fx fuzz analyze"."""
    fuzzer = factory.create_fuzzer(args)

    if args.corpora:
        for corpus in args.corpora:
            fuzzer.corpus.add_from_host(corpus)
    if args.dict:
        fuzzer.dictionary.replace(args.dict)
    if not args.local:
        gcs_url = 'gs://corpus.internal.clusterfuzz.com/libFuzzer/fuchsia_{}-{}'.format(
            fuzzer.package, fuzzer.executable)
        fuzzer.corpus.add_from_gcs(gcs_url)
    fuzzer.analyze()


def run_unittests(args, factory):
    """Runs unittests under the test directory."""
    lib_dir = os.path.dirname(os.path.abspath(__file__))
    test_dir = os.path.join(os.path.dirname(lib_dir), 'test')
    tests = unittest.defaultTestLoader.discover(test_dir, pattern='*_test.py')
    unittest.runner.TextTestRunner().run(tests)
