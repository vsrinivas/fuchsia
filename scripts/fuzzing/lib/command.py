#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys
import unittest

from host import Host
from fuzzer import Fuzzer

# This file contains the top-level implementations for each of the subcommands
# for "fx fuzz".


def list_fuzzers(args, factory):
    """Implementation of "fx fuzz list"."""
    buildenv = factory.create_buildenv()
    fuzzers = buildenv.fuzzers(args.name)
    if len(fuzzers) == 0:
        factory.host.echo('No matching fuzzers.')
    else:
        factory.host.echo('Found {} matching fuzzers:'.format(len(fuzzers)))
        for package, executable in fuzzers:
            factory.host.echo('  {}/{}'.format(package, executable))


def start_fuzzer(args, factory):
    """Implementation of "fx fuzz start"."""
    fuzzer = factory.create_fuzzer(args)
    if not args.monitor:
        factory.host.echo(
            'Starting {}.'.format(fuzzer),
            'Outputs will be written to: {}'.format(fuzzer.output))
        if not args.foreground:
            factory.host.echo(
                'Check status with "fx fuzz check {}".'.format(fuzzer),
                'Stop manually with "fx fuzz stop {}".'.format(fuzzer))
        fuzzer.start()
        if not args.foreground:
            cmd = ['python', sys.argv[0], 'start', '--monitor']
            if fuzzer.output:
                cmd += ['--output', fuzzer.output]
            cmd.append(str(fuzzer))
            return factory.host.create_process(cmd).popen()
    else:
        fuzzer.monitor()
        factory.host.echo(
            '{} has stopped.'.format(fuzzer),
            'Output written to: {}.'.format(fuzzer.output))


def check_fuzzer(args, factory):
    """Implementation of "fx fuzz check"."""
    device = factory.create_device()
    blank = True
    for package, executable in device.buildenv.fuzzers(args.name):
        fuzzer = Fuzzer(device, package, executable)
        if not args.name and not fuzzer.is_running():
            continue
        if not fuzzer.is_resolved():
            factory.host.echo('{}: NOT INSTALLED'.format(fuzzer))
        elif fuzzer.is_running():
            factory.host.echo('{}: RUNNING'.format(fuzzer))
        else:
            factory.host.echo('{}: STOPPED'.format(fuzzer))
        if fuzzer.is_resolved():
            num, size = fuzzer.corpus.measure()
            factory.host.echo(
                '    Corpus size:  {} inputs / {} bytes'.format(num, size))
        artifacts = fuzzer.list_artifacts()
        if artifacts:
            factory.host.echo('    Artifacts:')
            for artifact in artifacts:
                factory.host.echo('        {}'.format(artifact))
        factory.host.echo('')
        blank = False
    if blank:
        factory.host.echo(
            'No fuzzers are running.',
            'Include \'name\' to check specific fuzzers.')


def stop_fuzzer(args, factory):
    """Implementation of "fx fuzz stop"."""
    fuzzer = factory.create_fuzzer(args)
    if fuzzer.is_running():
        factory.host.echo('Stopping {}.'.format(fuzzer))
        fuzzer.stop()
    else:
        factory.host.echo('{} is already stopped.'.format(fuzzer))


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


def _run_tests(pattern, factory):
    lib_dir = os.path.dirname(os.path.abspath(__file__))
    test_dir = os.path.join(os.path.dirname(lib_dir), 'test')
    tests = unittest.defaultTestLoader.discover(test_dir, pattern=pattern)
    if factory.host.tracing:
        os.environ[Host.TRACE_ENVVAR] = '1'
        verbosity = 2
    else:
        verbosity = 1
    unittest.runner.TextTestRunner(verbosity=verbosity).run(tests)


def run_unittests(args, factory):
    """Runs unittests under the test directory."""
    _run_tests('*_test.py', factory)


def run_e2e_test(args, factory):
    """Runs the end-to-end test against a (resolved) fuzzer."""
    # Use Factory.create_fuzzer to resolve the fuzzer name, prompting the user
    # if needed.
    fuzzer = factory.create_fuzzer(args)
    sys.argv[sys.argv.index(args.name)] = str(fuzzer)
    _run_tests('test_e2e.py', factory)
