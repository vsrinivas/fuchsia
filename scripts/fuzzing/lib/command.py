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
    buildenv = factory.buildenv
    fuzzer_tests = buildenv.fuzzer_tests(args.name)
    match_prefix = ' matching' if args.name else ''
    match_suffix = ' for "{}"'.format(args.name) if args.name else ''
    if fuzzer_tests:
        plural = 's' if len(fuzzer_tests) != 1 else ''
        factory.host.echo(
            'Found {}{} fuzzer test{}{}:'.format(
                len(fuzzer_tests), match_prefix, plural, match_suffix))
        for fuzzer_test in fuzzer_tests:
            factory.host.echo('  ' + str(fuzzer_test) + '_test')

        if plural:
            target_was_not_selected = 'These tests correspond to fuzzers, but were not selected by the build arguments'
        else:
            target_was_not_selected = 'This test corresponds to a fuzzer, but was not selected by the build arguments'
        factory.host.echo(
            '', target_was_not_selected,
            'to be built with a fuzzer toolchain variant.', '',
            'To select them, you can use `fx set ... --fuzz-with <sanitizer>`.',
            'See https://fuchsia.dev/fuchsia-src/development/testing/fuzzing/build-a-fuzzer',
            'for additional details.', '')

    fuzzers = buildenv.fuzzers(args.name)
    if fuzzers:
        plural = 's' if len(fuzzers) != 1 else ''
        factory.host.echo(
            'Found {}{} fuzzer{}{}:'.format(
                len(fuzzers), match_prefix, plural, match_suffix))
        for fuzzer in fuzzers:
            factory.host.echo('  ' + str(fuzzer))
    else:
        factory.host.echo(
            'No{} fuzzers found{}.'.format(match_prefix, match_suffix))


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
    blank = True
    for fuzzer in factory.buildenv.fuzzers(args.name):
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


def update_corpus(args, factory):
    """Implementation of "fx fuzz update"."""
    # Factory.create_fuzzer interprets args.output as the place to store logs, which is not what we
    # want here.
    build_gn = None
    if args.output:
        build_gn = os.path.abspath(args.output)
    args.output = None
    fuzzer = factory.create_fuzzer(args, include_tests=True)
    elems = fuzzer.corpus.generate_buildfile(build_gn=build_gn)
    if len(elems) == 0:
        factory.host.echo('Empty corpus added.')
    else:
        factory.host.echo('Added:')
        for elem in elems:
            factory.host.echo('  ' + elem)
    if not build_gn:
        build_gn = '{}/BUILD.gn'.format(fuzzer.corpus.srcdir)
    else:
        build_gn = factory.buildenv.path(build_gn)
        build_gn = '//' + os.path.relpath(
            build_gn, factory.buildenv.fuchsia_dir)
    factory.host.echo('', build_gn + ' updated.')


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
