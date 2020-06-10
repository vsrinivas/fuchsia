#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys

from factory import Factory
from fuzzer import Fuzzer

# This file contains the top-level implementations for each of the subcommands
# for `fx fuzz`.


def list_fuzzers(args, factory):
    """Implementation of `fx fuzz list`."""
    if not factory:
        factory = Factory()
    host = factory.create_host()
    fuzzers = sorted(host.fuzzers(args.name))
    if len(fuzzers) == 0:
        factory.cli.echo('No matching fuzzers.')
    else:
        factory.cli.echo('Found {} matching fuzzers:'.format(len(fuzzers)))
        for package, executable in fuzzers:
            factory.cli.echo('  {}/{}'.format(package, executable))


def _monitor_cmd(fuzzer):
    """Command line to invoke fuzzer.monitor()."""
    cmd = ['python', sys.argv[0], '--monitor']
    if fuzzer.output:
        cmd += ['--output', fuzzer.output]
    return cmd + [str(fuzzer)]


def start_fuzzer(args, factory):
    """Implementation of `fx fuzz start`."""
    if not factory:
        factory = Factory()
    fuzzer = factory.create_fuzzer(args)
    if fuzzer.libfuzzer_inputs:
        factory.cli.error(
            'Passing corpus arguments to libFuzzer is unsupported',
            'You can run specific inputs with "fx fuzz repro".',
            'You can add elements to a seed corpus in GN.',
            'You can also analyze a corpus using `fx fuzz analyze`.')
    if not args.monitor:
        factory.cli.echo(
            ' Starting {}.'.format(fuzzer),
            ' Outputs will be written to: {}'.format(fuzzer.output))
        if not args.foreground:
            factory.cli.echo(
                'Check status with `fx fuzz check {}`.'.format(fuzzer),
                'Stop manually with `fx fuzz stop {}`.'.format(fuzzer))
        fuzzer.start()
        if not args.foreground:
            cmd = _monitor_cmd(fuzzer)
            fuzzer.device.host.create_process(cmd).popen()
    else:
        fuzzer.monitor()
        factory.cli.echo(
            '{} has stopped.'.format(fuzzer),
            'Output written to: {}.'.format(fuzzer.output))


def check_fuzzer(args, factory):
    """Implementation of `fx fuzz check`."""
    if not factory:
        factory = Factory()
    device = factory.create_device()
    status = None
    for package, executable in device.host.fuzzers(args.name):
        fuzzer = Fuzzer(device, package, executable)
        if not args.name and not fuzzer.is_running():
            continue
        status = 'RUNNING' if fuzzer.is_running() else 'STOPPED'
        num, size = fuzzer.measure_corpus()
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
    """Implementation of `fx fuzz stop`."""
    if not factory:
        factory = Factory()
    fuzzer = factory.create_fuzzer(args)
    if fuzzer.is_running():
        factory.cli.echo('Stopping {}.'.format(fuzzer))
        fuzzer.stop()
    else:
        factory.cli.echo('{} is already stopped.'.format(fuzzer))


def repro_units(args, factory):
    """Implementation of `fx fuzz repro`."""
    if not factory:
        factory = Factory()
    fuzzer = factory.create_fuzzer(args)
    if fuzzer.libfuzzer_inputs:
        fuzzer.repro()
    else:
        factory.cli.error('No artifacts provided.', 'Try `fx fuzz help`.')
