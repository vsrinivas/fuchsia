#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import re
import sys

import command
from host import Host

# This file contains the arguments parsing for "fx fuzz".


class ArgParser(argparse.ArgumentParser):
    """Wrapper to ArgumentParser that suppresses help and usage messages.

    argparse adds a lot of benefit, but its help and usage messages don't fit
    well with the style of other "fx" utilities. As a result, this class
    overrides the error() and exit() methods to suppress printing those
    messages. As an added bonus, the incorporation of Host
    makes it possible to test these conditions.

    Attributes:
        host:       The host object representing the current system.
    """

    # These are used in parse_args() to identify and extract libFuzzer arguments.
    LIBFUZZER_OPT_RE = re.compile(r'^-(\w+)=(.*)$')
    SHORT_OPT_RE = re.compile(r'^-\w$')
    LONG_OPT_RE = re.compile(r'^--\w+$')
    POSITIONAL_RE = re.compile(r'^[^-]')

    def __init__(self, **kwargs):
        # Don't use argparse's built-in help.
        kwargs['usage'] = ''
        kwargs['add_help'] = False
        super(ArgParser, self).__init__(**kwargs)
        self._parsers = {}
        self._unique_options = []
        self._options_help = []
        self._arguments_help = []
        self._has_libfuzzer_extras = False

    @property
    def host(self):
        """The system interface for user interactions."""
        assert self._host, 'Host not set.'
        return self._host

    @host.setter
    def host(self, host):
        self._host = host

    def add_parsers(self):
        """Configure a top-level parser with subparsers.

        Each subcommand should add a parser using the special SubparserAction
        returned by argparse.ArgumentParser.add_subparsers(). See
        https://docs.python.org/2/library/argparse.html#sub-commands.
        """
        self.usage = '[SUBCOMMAND] [...]'
        self.description = [
            'Manage Fuchsia fuzzers. SUBCOMMAND defaults to "start" if omitted.'
        ]

        # fx fuzz help
        help_parser = self._add_parser('help')
        help_parser.help = 'Print this message and exit.'
        help_parser.description = [
            'Prints the detailed help for SUBCOMMAND if provided, or a general help message.'
        ]
        help_parser._add_argument(
            'subcommand',
            nargs='?',
            help=['Subcommand for which to print detailed help.'])

        # fx fuzz list [name]
        list_parser = self._add_parser('list')
        list_parser.help = 'List available fuzzers in the current build.'
        list_parser.description = [
            'Lists fuzzers matching NAME if provided, or all fuzzers.'
        ]
        list_parser._add_verbose_flag()
        list_parser._add_name_argument(required=False)
        list_parser.set_defaults(command=command.list_fuzzers)

        # fx fuzz start [-d] [-f] [-m] [-o <output>] <name>
        start_parser = self._add_parser('start')
        start_parser.help = 'Start a specific fuzzer.'
        start_parser.description = ['Starts the named fuzzer.']
        start_parser._add_debug_flag()
        start_parser._add_flag(
            '-f', '--foreground', help=['Display fuzzer output.'])
        start_parser._add_flag('-m', '--monitor')
        start_parser._add_output_option()
        start_parser._add_verbose_flag()
        start_parser._add_name_argument(required=True)
        start_parser._add_libfuzzer_extras()
        start_parser.set_defaults(command=command.start_fuzzer)

        # fx fuzz check [name]
        check_parser = self._add_parser('check')
        check_parser.description = [
            'Reports status for the fuzzer matching NAME if provided, or for all running',
            'fuzzers. Status includes execution state, corpus size, and number of artifacts.',
        ]
        check_parser.help = 'Check on the status of one or more fuzzers.'
        check_parser._add_verbose_flag()
        check_parser._add_name_argument(required=False)
        check_parser.set_defaults(command=command.check_fuzzer)

        # fx fuzz stop <name>
        stop_parser = self._add_parser('stop')
        stop_parser.description = ['Stops the named fuzzer.']
        stop_parser.help = 'Stop a specific fuzzer.'
        stop_parser._add_verbose_flag()
        stop_parser._add_name_argument(required=True)
        stop_parser.set_defaults(command=command.stop_fuzzer)

        # fx fuzz repro [-d] [-o <output>] <name> <file>...
        repro_parser = self._add_parser('repro')
        repro_parser.description = [
            'Runs the named fuzzer on provided test units.'
        ]
        repro_parser.help = 'Reproduce fuzzer findings by replaying test units.'
        repro_parser._add_debug_flag()
        repro_parser._add_output_option()
        repro_parser._add_verbose_flag()
        repro_parser._add_name_argument(required=True)
        repro_parser._add_argument(
            'libfuzzer_inputs',
            metavar='unit',
            nargs='+',
            help=[
                'File containing a fuzzer input, such as an artifact from a',
                'previous fuzzer run. Artifacts are typically named by the',
                'type of artifact and a digest of the fuzzer input, e.g.',
                'crash-2c5d0d1831b242b107a4c42bba2fa3f6d85edc35',
            ])
        repro_parser._add_libfuzzer_extras()
        repro_parser.set_defaults(command=command.repro_units)

        # fx fuzz analyze [-c <dir>] [-d <file>] [-l] [-o <output>] <name>
        analyze_parser = self._add_parser('analyze')
        analyze_parser.description = [
            'Analyze the corpus and/or dictionary for the given fuzzer.'
        ]

        analyze_parser.help = 'Report coverage info for a given corpus and/or dictionary.'
        analyze_parser.add_option(
            '-c',
            '--corpus',
            dest='corpora',
            help=['Path to additional corpus elements. May be repeated.'])
        analyze_parser.add_option(
            '-d',
            '--dict',
            unique=True,
            help=['Path to a fuzzer dictionary. Replaces the package default.'])
        analyze_parser._add_flag(
            '-l', '--local', help=['Exclude corpus elements from Clusterfuzz.'])
        analyze_parser._add_output_option()
        analyze_parser._add_verbose_flag()
        analyze_parser._add_name_argument(required=True)
        analyze_parser._add_libfuzzer_extras()
        analyze_parser.set_defaults(command=command.analyze_fuzzer)

        update_parser = self._add_parser('update')
        update_parser.description = [
            'Update the BUILD.gn file for a fuzzer corpus.'
        ]

        update_parser.help = 'Update the BUILD.gn file for a fuzzer corpus.'
        update_parser._add_output_option()
        update_parser._add_verbose_flag()
        update_parser._add_name_argument(required=True)
        update_parser.set_defaults(command=command.update_corpus)

        unittest_parser = self._add_parser('unittest')
        unittest_parser.description = [
            'Run the unittests for this tool. This runs all tests from all test cases. To run',
            'a single test, use "python <path/to/test.py> <test_name>" instead.'
        ]
        unittest_parser._add_verbose_flag()
        unittest_parser.help = 'Run the unittests for this tool.'
        unittest_parser.set_defaults(command=command.run_unittests)

        e2e_test_parser = self._add_parser('e2etest')
        e2e_test_parser.description = [
            'Run the end-to-end test for this tool using the named fuzzer. This requires the',
            'fuzzer to have already been built and deployed to a running device.'
        ]
        e2e_test_parser._add_flag(
            '-l', '--local', help=['Exclude corpus elements from Clusterfuzz.'])
        e2e_test_parser._add_verbose_flag()
        e2e_test_parser._add_name_argument(required=True)
        e2e_test_parser.help = 'Run the end-to-end test for this tool.'
        e2e_test_parser.set_defaults(command=command.run_e2e_test)

        # TODO(fxb/24828) Once this is ready, merge with analyze or another tool
        coverage_parser = self._add_parser('coverage')
        coverage_parser.description = [
            '[EXPERIMENTAL] Generates a coverage report for a set of tests.',
            'Requires --variant profile to be set via fx set to generate the',
            'necessary symbols. It is suggested to run with --no-goma in order',
            'to preserve linking to files in the report.',
        ]
        coverage_parser._add_verbose_flag()
        coverage_parser._add_name_argument(required=True)
        coverage_parser._add_output_option()
        coverage_parser.help = 'Generate a coverage report for a test.'
        coverage_parser.set_defaults(command=command.measure_coverage)

        self.epilog = [
            'See "fx fuzz help [SUBCOMMAND]" for details on each subcommand.',
            'See also "fx help fuzz" for global "fx" options.',
            'See https://fuchsia.dev/fuchsia-src/development/testing/fuzzing/libfuzzer',
            'for details on writing and building fuzzers.',
        ]

    def _add_parser(self, subcommand):
        """Return a subparser for a specific subcommand."""
        parser = ArgParser(prog=subcommand)
        parser.host = self.host
        self._parsers[subcommand] = parser
        return parser

    def _format_help(self, item, help_msg):
        """Format a help message for an argument or option.

        If help_msg is None, the item will be suppressed from the help entirely.
        """
        lines = []
        if help_msg:
            lines += ['  {}'.format(item).ljust(22) + help_msg[0]]
            lines += [(' ' * 22) + line for line in help_msg[1:]]
        return lines

    def _add_flag(self, short_opt, long_opt, **kwargs):
        """Add an optional command line boolean flag.

        If help_msg is None, the option is suppressed from the help message.
        """
        help_msg = kwargs.pop('help', None)
        item = '{},{}'.format(short_opt, long_opt)
        self._options_help += self._format_help(item, help_msg)
        kwargs['action'] = 'store_true'
        self.add_argument(short_opt, long_opt, **kwargs)

    def add_option(self, short_opt, long_opt, **kwargs):
        """Add an command line option that takes a parameter.

        If unique is False, the option may be repeated.
        If help_msg is None, the option is suppressed from the help message.
        """
        help_msg = kwargs.pop('help', None)
        metavar = kwargs.pop('metavar', long_opt[2:].upper())
        unique = kwargs.pop('unique', False)
        item = '{},{} {}'.format(short_opt, long_opt, metavar)
        self._options_help += self._format_help(item, help_msg)
        if unique:
            self._unique_options.append(long_opt[2:])
        kwargs['action'] = 'append'
        self.add_argument(short_opt, long_opt, **kwargs)

    def _add_argument(self, name, **kwargs):
        """Add a positional command line argument.

        If help is None, the argument is suppressed from the help message.
        """
        help_msg = kwargs.pop('help', None)
        metavar = kwargs.pop('metavar', name).upper()
        nargs = kwargs.get('nargs', None)
        usage = metavar
        if nargs == '+' or nargs == '*':
            usage = '{}...'.format(usage)
        if nargs == '?' or nargs == '*':
            usage = '[{}]'.format(usage)
        self.usage += ' {}'.format(usage)
        self._arguments_help += self._format_help(metavar, help_msg)
        self.add_argument(name, **kwargs)

    def _add_name_argument(self, required=False):
        """Adds a fuzzer "NAME" argument.

        If required is True, a fuzzer name must be supplied.
        """
        self._add_argument(
            'name',
            nargs=None if required else '?',
            help=[
                'Fuzzer name to match.  This can be part of the package',
                'and/or target name, e.g. "foo", "bar", and "foo/bar" all',
                'match "foo_package/bar_target".',
            ])

    def _add_debug_flag(self):
        """Adds a "--debug" flag."""
        self._add_flag(
            '-g',
            '--debug',
            help=['Disable exception handling so a debugger can be attached'])

    def _add_output_option(self):
        """Adds an "--output OUTPUT" option."""
        self.add_option(
            '-o',
            '--output',
            unique=True,
            help=['Path under which to store results.'])

    def _add_verbose_flag(self):
        """Adds a "--verbose" flag."""
        self._add_flag('-v', '--verbose', help=['Display additional output.'])

    def _add_libfuzzer_extras(self):
        """Adds liFuzzer options and subprocess arguments.

        If a name is given, it will be listed as an argument.
        """
        if not self._has_libfuzzer_extras:
            self._has_libfuzzer_extras = True
            self.usage += ' [...]'
            self.epilog = [
                'Additional options and/or arguments are passed through to libFuzzer.',
                'See https://llvm.org/docs/LibFuzzer.html for details.',
            ]

    def parse_args(self, args=None):
        """Parse args either as a top-level parser or subcommand."""
        if args == None:
            args = sys.argv[1:]
        if self._parsers:
            return self._dispatch_to_subcommand(args)
        else:
            return self._parse_subcommand_args(args)

    def _dispatch_to_subcommand(self, args):
        """Invokes the correct subcommand subparser."""
        if not args or args[0] in ['-h', '--help']:
            subcommand = 'help'
            args = []
        elif args[0] not in self._parsers:
            subcommand = 'start'
        else:
            subcommand = args[0]
            args = args[1:]
        self.host.trace('fx fuzz {}'.format(' '.join(args)))
        args = self._parsers[subcommand].parse_args(args)
        if subcommand != 'help':
            return args
        if not args.subcommand:
            self.host.echo(*self.generate_help())
        elif args.subcommand in self._parsers:
            self.host.echo(*self._parsers[args.subcommand].generate_help())
        else:
            self.error('Unrecognized subcommand: "{}".'.format(args.subcommand))
        self.exit()

    def _parse_subcommand_args(self, args):
        """Extract libFuzzer arguments and parse the rest.

        This method extracts the libfuzzer options (of the form "-key=val") and
        the subprocess arguments (which follow "--") to avoid them getting
        mangled by argparse. The positional libFuzzer inputs are collected as
        expected, see _add_libfuzzer_inputs().
        """
        if self._has_libfuzzer_extras:
            libfuzzer_opts = {}
            pass_to_subprocess = False
            subprocess_args = []
            valid = []
            for arg in args:
                libfuzzer_opt = ArgParser.LIBFUZZER_OPT_RE.match(arg)
                if pass_to_subprocess:
                    subprocess_args.append(arg)
                elif arg == '--':
                    pass_to_subprocess = True
                elif libfuzzer_opt:
                    libfuzzer_opts[libfuzzer_opt.group(
                        1)] = libfuzzer_opt.group(2)
                elif (ArgParser.SHORT_OPT_RE.match(arg) or
                      ArgParser.LONG_OPT_RE.match(arg) or
                      ArgParser.POSITIONAL_RE.match(arg)):
                    valid.append(arg)
                else:
                    self.error('Unrecognized option: {}'.format(arg))
            args = valid

        # Parse!
        args = super(ArgParser, self).parse_args(args)

        # Check for options that were incorrectly repeated
        for key in self._unique_options:
            val = getattr(args, key, None)
            if not val:
                continue
            if len(val) == 1:
                setattr(args, key, val[0])
            else:
                self.error('Repeated option: {}'.format(key))

        # Forward libFuzzer arguments
        if self._has_libfuzzer_extras:
            args.libfuzzer_opts = libfuzzer_opts
            args.subprocess_args = subprocess_args

        return args

    def generate_help(self):
        """Builds the help message as a list of lines.

           Example output:
             Usage: fx fuzz foobar [OPTIONS] NAME [...]

             Arguments:
               NAME         The thing it's called

             Options:
               --foo BAR    A baz, and then some.
        """
        lines = ['']
        usage = 'Usage: fx fuzz '
        if not self._parsers:
            usage += self.prog
        if self._options_help:
            usage += ' [OPTIONS]'
        usage += self.usage
        lines += [usage]

        if self.description:
            lines += ['']
            lines += self.description

        if self._parsers:
            lines += ['']
            lines += ['Subcommands:']
            for prog, subcommand in sorted(self._parsers.iteritems()):
                lines += self._format_help(prog, [subcommand.help])

        if self._arguments_help:
            lines += ['']
            lines += ['Arguments:']
            lines += self._arguments_help

        if self._options_help:
            lines += ['']
            lines += ['Options:']
            lines += self._options_help

        if self.epilog:
            lines += ['']
            lines += self.epilog

        lines += ['']

        for line in lines:
            assert len(line) <= 80, 'Line is too long:\n"{}"'.format(line)

        return lines

    def exit(self, status=0, message=None):
        if message:
            self.host.error(message, 'Try "fx fuzz help".')
        sys.exit(status)

    def error(self, message):
        """Prints an error message and exits.

           Cleans up the argparse messages before passing them to exit, e.g.
           argparse's messages are lowercase and unpunctuated.
        """
        if ':' not in message and not message.endswith('.'):
            message += '.'
        self.exit(2, message.capitalize())
