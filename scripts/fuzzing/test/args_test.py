#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

import test_env
import lib.command as command

from factory_fake import FakeFactory


class TestArgs(unittest.TestCase):

    # Unit test assertions

    def assertParse(self, factory, args, **kwargs):
        parser = factory.create_parser()
        args = vars(parser.parse_args(args))
        for key, val in kwargs.iteritems():
            self.assertEqual(args[key], val)

    def assertParseFails(self, factory, args, msg):
        parser = factory.create_parser()
        with self.assertRaises(SystemExit):
            parser.parse_args(args)
        self.assertEqual(
            factory.cli.log, [
                'ERROR: {}'.format(msg),
                '       Try "fx fuzz help".',
            ])

    def assertParseHelp(self, factory, args, log):
        parser = factory.create_parser()
        with self.assertRaises(SystemExit):
            parser.parse_args(args)
        self.assertEqual(factory.cli.log, log)

    # Unit tests

    def test_help_parser(self):
        factory = FakeFactory()
        generic_help = [
            '',
            'Usage: fx fuzz [SUBCOMMAND] [...]',
            '',
            'Manage Fuchsia fuzzers. SUBCOMMAND defaults to "start" if omitted.',
            '',
            'Subcommands:',
            '  check               Check on the status of one or more fuzzers.',
            '  help                Print this message and exit.',
            '  list                List available fuzzers in the current build.',
            '  repro               Reproduce fuzzer findings by replaying test units.',
            '  start               Start a specific fuzzer.',
            '  stop                Stop a specific fuzzer.',
            '',
            'See "fx fuzz help [SUBCOMMAND]" for details on each subcommand.',
            'See also "fx help fuzz" for global "fx" options.',
            'See https://fuchsia.dev/fuchsia-src/development/testing/fuzzing/libfuzzer',
            'for details on writing and building fuzzers.',
            '',
        ]
        self.assertParseHelp(
            factory, ['help', 'help'], [
                '',
                'Usage: fx fuzz help [SUBCOMMAND]',
                '',
                'Prints the detailed help for SUBCOMMAND if provided, or a general help message.',
                '',
                'Arguments:',
                '  SUBCOMMAND          Subcommand for which to print detailed help.',
                '',
            ])
        self.assertParseHelp(factory, ['-h'], generic_help)
        self.assertParseHelp(factory, ['--help'], generic_help)
        self.assertParseHelp(factory, ['help'], generic_help)

        self.assertParseFails(
            factory, ['help', 'bad-subcommand'],
            'Unrecognized subcommand: "bad-subcommand".')

    def test_list_parser(self):
        factory = FakeFactory()
        self.assertParseHelp(
            factory, ['help', 'list'], [
                '',
                'Usage: fx fuzz list [NAME]',
                '',
                'Lists fuzzers matching NAME if provided, or all fuzzers.',
                '',
                'Arguments:',
                '  NAME                Fuzzer name to match.  This can be part of the package',
                '                      and/or target name, e.g. "foo", "bar", and "foo/bar" all',
                '                      match "foo_package/bar_target".',
                '',
            ])
        self.assertParse(
            factory, ['list'], command=command.list_fuzzers, name=None)
        self.assertParse(factory, ['list', 'name'], name='name')
        self.assertParseFails(
            factory, ['list', 'name', 'extra'], 'Unrecognized arguments: extra')

    def test_start_parser(self):
        factory = FakeFactory()
        self.assertParseHelp(
            factory, ['help', 'start'], [
                '',
                'Usage: fx fuzz start [OPTIONS] NAME [...]',
                '',
                'Starts the named fuzzer.',
                '',
                'Arguments:',
                '  NAME                Fuzzer name to match.  This can be part of the package',
                '                      and/or target name, e.g. "foo", "bar", and "foo/bar" all',
                '                      match "foo_package/bar_target".',
                '',
                'Options:',
                '  -g,--debug          Disable exception handling so a debugger can be attached',
                '  -f,--foreground     Display fuzzer output.',
                '  -o,--output OUTPUT  Path under which to store results.',
                '',
                'Additional options and/or arguments are passed through to libFuzzer.',
                'See https://llvm.org/docs/LibFuzzer.html for details.',
                '',
            ])

        self.assertParseFails(factory, ['start'], 'Too few arguments.')

        self.assertParse(
            factory, [
                'start',
                'name',
            ],
            command=command.start_fuzzer,
            name='name')
        self.assertParse(
            factory, ['name'], command=command.start_fuzzer, name='name')

        self.assertParse(factory, ['start', '--debug', 'name'], debug=True)
        self.assertParse(factory, ['start', '-g', 'name'], debug=True)

        self.assertParse(
            factory, ['start', '--foreground', 'name'], foreground=True)
        self.assertParse(factory, ['start', '-f', 'name'], foreground=True)

        self.assertParse(factory, ['start', '--monitor', 'name'], monitor=True)
        self.assertParse(factory, ['start', '-m', 'name'], monitor=True)

        self.assertParseFails(
            factory, ['start', '--output', 'name'], 'Too few arguments.')
        self.assertParseFails(
            factory, [
                'start',
                '--output',
                'output1',
                '--output',
                'output2',
                'name',
            ], 'Repeated option: output')
        self.assertParse(
            factory, [
                'start',
                '-o',
                'output',
                'name',
            ], output='output')
        self.assertParse(
            factory, [
                'start',
                '--output',
                'output',
                'name',
            ], output='output')

        self.assertParse(
            factory, [
                'start',
                'name',
                '-output=foo',
            ],
            libfuzzer_opts={
                'output': 'foo',
            })

        self.assertParseFails(
            factory, [
                'start',
                'name',
                'input1',
                'input2',
            ], 'Unrecognized arguments: input1 input2')

        self.assertParse(
            factory, [
                'start',
                'name',
                '--',
                'sub1',
                'sub2',
            ],
            subprocess_args=[
                'sub1',
                'sub2',
            ])

        # All together now, in different order.
        self.assertParse(
            factory, [
                'start',
                '-output=foo',
                'name',
                '--output',
                'output',
                '--monitor',
                '--foreground',
                '--debug',
                '--',
                '-o',
                '--output',
                '-output=bar',
            ],
            command=command.start_fuzzer,
            debug=True,
            foreground=True,
            monitor=True,
            output='output',
            name='name',
            libfuzzer_opts={'output': 'foo'},
            subprocess_args=['-o', '--output', '-output=bar'])

    def test_check_parser(self):
        factory = FakeFactory()
        self.assertParseHelp(
            factory, ['help', 'check'], [
                '',
                'Usage: fx fuzz check [NAME]',
                '',
                'Reports status for the fuzzer matching NAME if provided, or for all running',
                'fuzzers. Status includes execution state, corpus size, and number of artifacts.',
                '',
                'Arguments:',
                '  NAME                Fuzzer name to match.  This can be part of the package',
                '                      and/or target name, e.g. "foo", "bar", and "foo/bar" all',
                '                      match "foo_package/bar_target".',
                '',
            ])
        self.assertParse(
            factory, ['check'], command=command.check_fuzzer, name=None)
        self.assertParse(factory, ['check', 'name'], name='name')
        self.assertParseFails(
            factory, ['check', 'name', 'extra'],
            'Unrecognized arguments: extra')

    def test_stop_parser(self):
        factory = FakeFactory()
        self.assertParseHelp(
            factory, ['help', 'stop'], [
                '',
                'Usage: fx fuzz stop NAME',
                '',
                'Stops the named fuzzer.',
                '',
                'Arguments:',
                '  NAME                Fuzzer name to match.  This can be part of the package',
                '                      and/or target name, e.g. "foo", "bar", and "foo/bar" all',
                '                      match "foo_package/bar_target".',
                '',
            ])
        self.assertParseFails(factory, ['stop'], 'Too few arguments.')

        self.assertParse(
            factory, [
                'stop',
                'name',
            ],
            command=command.stop_fuzzer,
            name='name')

        self.assertParseFails(
            factory, ['stop', 'name', 'extra'], 'Unrecognized arguments: extra')

    def test_repro_parser(self):
        factory = FakeFactory()
        self.assertParseHelp(
            factory, ['help', 'repro'], [
                '',
                'Usage: fx fuzz repro [OPTIONS] NAME UNIT... [...]',
                '',
                'Runs the named fuzzer on provided test units.',
                '',
                'Arguments:',
                '  NAME                Fuzzer name to match.  This can be part of the package',
                '                      and/or target name, e.g. "foo", "bar", and "foo/bar" all',
                '                      match "foo_package/bar_target".',
                '  UNIT                File containing a fuzzer input, such as an artifact from a',
                '                      previous fuzzer run. Artifacts are typically named by the',
                '                      type of artifact and a digest of the fuzzer input, e.g.',
                '                      crash-2c5d0d1831b242b107a4c42bba2fa3f6d85edc35',
                '',
                'Options:',
                '  -g,--debug          Disable exception handling so a debugger can be attached',
                '  -o,--output OUTPUT  Path under which to store results.',
                '',
                'Additional options and/or arguments are passed through to libFuzzer.',
                'See https://llvm.org/docs/LibFuzzer.html for details.',
                '',
            ])

        self.assertParseFails(factory, ['repro'], 'Too few arguments.')

        self.assertParse(
            factory, [
                'repro',
                'name',
                'unit',
            ],
            command=command.repro_units,
            name='name')

        self.assertParse(
            factory, ['repro', '--debug', 'name', 'unit'], debug=True)
        self.assertParse(factory, ['repro', '-g', 'name', 'unit'], debug=True)

        self.assertParseFails(
            factory, ['repro', '--output', 'name'], 'Too few arguments.')
        self.assertParseFails(
            factory, [
                'repro',
                '--output',
                'output1',
                '--output',
                'output2',
                'name',
                'unit',
            ], 'Repeated option: output')
        self.assertParse(
            factory, [
                'repro',
                '--output',
                'output',
                'name',
                'unit',
            ],
            output='output')
        self.assertParse(
            factory, [
                'repro',
                '-o',
                'output',
                'name',
                'unit',
            ],
            output='output')

        self.assertParse(
            factory, [
                'repro',
                'name',
                '-output=foo',
                'unit',
            ],
            libfuzzer_opts={
                'output': 'foo',
            })

        self.assertParse(
            factory, [
                'repro',
                'name',
                'input1',
                'input2',
            ],
            libfuzzer_inputs=[
                'input1',
                'input2',
            ])

        self.assertParse(
            factory, [
                'repro',
                'name',
                'unit',
                '--',
                'sub1',
                'sub2',
            ],
            subprocess_args=[
                'sub1',
                'sub2',
            ])

        # All together now, in different order.
        self.assertParse(
            factory, [
                'repro',
                'name',
                'input',
                '--output',
                'output',
                '-output=foo',
                '--debug',
                '--',
                '-o',
                '-output=bar',
                '--output',
            ],
            command=command.repro_units,
            debug=True,
            output='output',
            name='name',
            libfuzzer_opts={'output': 'foo'},
            libfuzzer_inputs=['input'],
            subprocess_args=['-o', '-output=bar', '--output'])


if __name__ == '__main__':
    unittest.main()
