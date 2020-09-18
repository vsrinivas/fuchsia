#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

import test_env
import lib.command as command
from test_case import TestCaseWithFactory


class ArgsTest(TestCaseWithFactory):

    # Unit test assertions

    def assertParse(self, args, **kwargs):
        args = vars(self.parse_args(*args))
        for key, val in kwargs.iteritems():
            self.assertEqual(args[key], val)

    def assertParseFails(self, args, msg):
        self.assertError(
            lambda: self.parse_args(*args), msg, 'Try "fx fuzz help".')

    def assertParseHelp(self, args, log):
        with self.assertRaises(SystemExit):
            self.parse_args(*args)
        self.assertLogged(*log)

    # Unit tests

    # Unit tests

    def test_help_parser(self):
        generic_help = [
            '',
            'Usage: fx fuzz [SUBCOMMAND] [...]',
            '',
            'Manage Fuchsia fuzzers. SUBCOMMAND defaults to "start" if omitted.',
            '',
            'Subcommands:',
            '  analyze             Report coverage info for a given corpus and/or dictionary.',
            '  check               Check on the status of one or more fuzzers.',
            '  e2etest             Run the end-to-end test for this tool.',
            '  help                Print this message and exit.',
            '  list                List available fuzzers in the current build.',
            '  repro               Reproduce fuzzer findings by replaying test units.',
            '  start               Start a specific fuzzer.',
            '  stop                Stop a specific fuzzer.',
            '  unittest            Run the unittests for this tool.',
            '  update              Update the BUILD.gn file for a fuzzer corpus.',
            '',
            'See "fx fuzz help [SUBCOMMAND]" for details on each subcommand.',
            'See also "fx help fuzz" for global "fx" options.',
            'See https://fuchsia.dev/fuchsia-src/development/testing/fuzzing/libfuzzer',
            'for details on writing and building fuzzers.',
            '',
        ]
        self.assertParseHelp(
            ['help', 'help'], [
                '',
                'Usage: fx fuzz help [SUBCOMMAND]',
                '',
                'Prints the detailed help for SUBCOMMAND if provided, or a general help message.',
                '',
                'Arguments:',
                '  SUBCOMMAND          Subcommand for which to print detailed help.',
                '',
            ])
        self.assertParseHelp(['-h'], generic_help)
        self.assertParseHelp(['--help'], generic_help)
        self.assertParseHelp(['help'], generic_help)

        self.assertParseFails(
            ['help', 'bad-subcommand'],
            'Unrecognized subcommand: "bad-subcommand".')

    def test_list_parser(self):
        self.assertParseHelp(
            ['help', 'list'], [
                '',
                'Usage: fx fuzz list [OPTIONS] [NAME]',
                '',
                'Lists fuzzers matching NAME if provided, or all fuzzers.',
                '',
                'Arguments:',
                '  NAME                Fuzzer name to match.  This can be part of the package',
                '                      and/or target name, e.g. "foo", "bar", and "foo/bar" all',
                '                      match "foo_package/bar_target".',
                '',
                'Options:',
                '  -v,--verbose        Display additional output.',
                '',
            ])
        self.assertParse(['list'], command=command.list_fuzzers, name=None)
        self.assertParse(['list', 'name'], name='name')
        self.assertParseFails(
            ['list', 'name', 'extra'], 'Unrecognized arguments: extra')

    def test_start_parser(self):
        self.assertParseHelp(
            ['help', 'start'], [
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
                '  -v,--verbose        Display additional output.',
                '',
                'Additional options and/or arguments are passed through to libFuzzer.',
                'See https://llvm.org/docs/LibFuzzer.html for details.',
                '',
            ])

        self.assertParseFails(['start'], 'Too few arguments.')

        self.assertParse(
            [
                'start',
                'name',
            ], command=command.start_fuzzer, name='name')
        self.assertParse(['name'], command=command.start_fuzzer, name='name')

        self.assertParse(['start', '--debug', 'name'], debug=True)
        self.assertParse(['start', '-g', 'name'], debug=True)

        self.assertParse(['start', '--foreground', 'name'], foreground=True)
        self.assertParse(['start', '-f', 'name'], foreground=True)

        self.assertParse(['start', '--monitor', 'name'], monitor=True)
        self.assertParse(['start', '-m', 'name'], monitor=True)

        self.assertParseFails(
            ['start', '--output', 'name'], 'Too few arguments.')
        self.assertParseFails(
            [
                'start',
                '--output',
                'output1',
                '--output',
                'output2',
                'name',
            ], 'Repeated option: output')
        self.assertParse([
            'start',
            '-o',
            'output',
            'name',
        ], output='output')
        self.assertParse(
            [
                'start',
                '--output',
                'output',
                'name',
            ], output='output')

        self.assertParse(
            [
                'start',
                'name',
                '-output=foo',
            ],
            libfuzzer_opts={
                'output': 'foo',
            })

        self.assertParseFails(
            [
                'start',
                'name',
                'input1',
                'input2',
            ], 'Unrecognized arguments: input1 input2')

        self.assertParse(
            [
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
            [
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
        self.assertParseHelp(
            ['help', 'check'], [
                '',
                'Usage: fx fuzz check [OPTIONS] [NAME]',
                '',
                'Reports status for the fuzzer matching NAME if provided, or for all running',
                'fuzzers. Status includes execution state, corpus size, and number of artifacts.',
                '',
                'Arguments:',
                '  NAME                Fuzzer name to match.  This can be part of the package',
                '                      and/or target name, e.g. "foo", "bar", and "foo/bar" all',
                '                      match "foo_package/bar_target".',
                '',
                'Options:',
                '  -v,--verbose        Display additional output.',
                '',
            ])
        self.assertParse(['check'], command=command.check_fuzzer, name=None)
        self.assertParse(['check', 'name'], name='name')
        self.assertParseFails(
            ['check', 'name', 'extra'], 'Unrecognized arguments: extra')

    def test_stop_parser(self):
        self.assertParseHelp(
            ['help', 'stop'], [
                '',
                'Usage: fx fuzz stop [OPTIONS] NAME',
                '',
                'Stops the named fuzzer.',
                '',
                'Arguments:',
                '  NAME                Fuzzer name to match.  This can be part of the package',
                '                      and/or target name, e.g. "foo", "bar", and "foo/bar" all',
                '                      match "foo_package/bar_target".',
                '',
                'Options:',
                '  -v,--verbose        Display additional output.',
                '',
            ])
        self.assertParseFails(['stop'], 'Too few arguments.')

        self.assertParse(
            [
                'stop',
                'name',
            ], command=command.stop_fuzzer, name='name')

        self.assertParseFails(
            ['stop', 'name', 'extra'], 'Unrecognized arguments: extra')

    def test_repro_parser(self):
        self.assertParseHelp(
            ['help', 'repro'], [
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
                '  -v,--verbose        Display additional output.',
                '',
                'Additional options and/or arguments are passed through to libFuzzer.',
                'See https://llvm.org/docs/LibFuzzer.html for details.',
                '',
            ])

        self.assertParseFails(['repro'], 'Too few arguments.')
        self.assertParseFails(['repro', 'name'], 'Too few arguments.')

        self.assertParse(
            [
                'repro',
                'name',
                'unit',
            ],
            command=command.repro_units,
            name='name',
            libfuzzer_inputs=[
                'unit',
            ])

        self.assertParse(['repro', '--debug', 'name', 'unit'], debug=True)
        self.assertParse(['repro', '-g', 'name', 'unit'], debug=True)

        self.assertParseFails(
            [
                'repro',
                '--output',
                'name',
                'unit',
            ], 'Too few arguments.')
        self.assertParseFails(
            [
                'repro',
                '--output',
                'output1',
                '--output',
                'output2',
                'name',
                'unit',
            ], 'Repeated option: output')
        self.assertParse(
            [
                'repro',
                '-o',
                'output',
                'name',
                'unit',
            ], output='output')
        self.assertParse(
            [
                'repro',
                '-o',
                'output',
                'name',
                'unit',
            ], output='output')

        self.assertParse(
            [
                'repro',
                'name',
                '-output=foo',
                'unit',
            ],
            libfuzzer_opts={
                'output': 'foo',
            })

        self.assertParse(
            [
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
            [
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

    def test_analyze_parser(self):
        self.assertParseHelp(
            ['help', 'analyze'], [
                '',
                'Usage: fx fuzz analyze [OPTIONS] NAME [...]',
                '',
                'Analyze the corpus and/or dictionary for the given fuzzer.',
                '',
                'Arguments:',
                '  NAME                Fuzzer name to match.  This can be part of the package',
                '                      and/or target name, e.g. "foo", "bar", and "foo/bar" all',
                '                      match "foo_package/bar_target".',
                '',
                'Options:',
                '  -c,--corpus CORPUS  Path to additional corpus elements. May be repeated.',
                '  -d,--dict DICT      Path to a fuzzer dictionary. Replaces the package default.',
                '  -l,--local          Exclude corpus elements from Clusterfuzz.',
                '  -o,--output OUTPUT  Path under which to store results.',
                '  -v,--verbose        Display additional output.',
                '',
                'Additional options and/or arguments are passed through to libFuzzer.',
                'See https://llvm.org/docs/LibFuzzer.html for details.',
                '',
            ])

        self.assertParseFails(['analyze'], 'Too few arguments.')

        self.assertParse(
            [
                'analyze',
                'name',
            ], command=command.analyze_fuzzer, name='name')

        self.assertParseFails(
            ['analyze', '--corpus', 'name'], 'Too few arguments.')
        self.assertParse(
            [
                'analyze',
                '--corpus',
                'corpus1',
                '--corpus',
                'corpus2',
                'name',
            ],
            corpora=['corpus1', 'corpus2'])
        self.assertParse(
            [
                'analyze',
                '-c',
                'corpus1',
                '-c',
                'corpus2',
                'name',
            ],
            corpora=['corpus1', 'corpus2'])

        self.assertParseFails(
            ['analyze', '--dict', 'name'], 'Too few arguments.')
        self.assertParseFails(
            [
                'analyze',
                '--dict',
                'dict1',
                '--dict',
                'dict2',
                'name',
            ], 'Repeated option: dict')
        self.assertParse([
            'analyze',
            '--dict',
            'dict',
            'name',
        ], dict='dict')
        self.assertParse([
            'analyze',
            '-d',
            'dict',
            'name',
        ], dict='dict')

        self.assertParse(['analyze', '--local', 'name'], local=True)
        self.assertParse(['analyze', '-l', 'name'], local=True)

        self.assertParseFails(
            ['analyze', '--output', 'name'], 'Too few arguments.')
        self.assertParseFails(
            [
                'analyze',
                '--output',
                'output1',
                '--output',
                'output2',
                'name',
            ], 'Repeated option: output')
        self.assertParse(
            [
                'analyze',
                '--output',
                'output',
                'name',
            ], output='output')
        self.assertParse([
            'analyze',
            '-o',
            'output',
            'name',
        ], output='output')

        self.assertParse(
            [
                'analyze',
                'name',
                '-output=foo',
                '--',
                'sub1',
                'sub2',
            ],
            libfuzzer_opts={'output': 'foo'})

        self.assertParse(
            [
                'analyze',
                'name',
                '-output=foo',
                '--',
                '--output',
                '-output=output',
            ],
            subprocess_args=[
                '--output',
                '-output=output',
            ])

        # All together now, in a different order.
        self.assertParse(
            [
                'analyze',
                '-output=foo',
                'name',
                '--output',
                'output',
                '--dict',
                'dict',
                '--corpus',
                'corpus',
                '--local',
                '--',
                '--sub',
                '-sub=val',
            ],
            command=command.analyze_fuzzer,
            corpora=['corpus'],
            dict='dict',
            local=True,
            output='output',
            name='name',
            libfuzzer_opts={'output': 'foo'},
            subprocess_args=['--sub', '-sub=val'])

    def test_unittest_parser(self):
        self.assertParseHelp(
            ['help', 'unittest'], [
                '',
                'Usage: fx fuzz unittest [OPTIONS]',
                '',
                'Run the unittests for this tool. This runs all tests from all test cases. To run',
                'a single test, use "python <path/to/test.py> <test_name>" instead.',
                '',
                'Options:',
                '  -v,--verbose        Display additional output.',
                '',
            ])

    def test_e2e_test_parser(self):
        self.assertParseHelp(
            ['help', 'e2etest'], [
                '',
                'Usage: fx fuzz e2etest [OPTIONS] NAME',
                '',
                'Run the end-to-end test for this tool using the named fuzzer. This requires the',
                'fuzzer to have already been built and deployed to a running device.',
                '',
                'Arguments:',
                '  NAME                Fuzzer name to match.  This can be part of the package',
                '                      and/or target name, e.g. "foo", "bar", and "foo/bar" all',
                '                      match "foo_package/bar_target".',
                '',
                'Options:',
                '  -l,--local          Exclude corpus elements from Clusterfuzz.',
                '  -v,--verbose        Display additional output.',
                '',
            ])


if __name__ == '__main__':
    unittest.main()
