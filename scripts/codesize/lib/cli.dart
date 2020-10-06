// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:io';

import 'package:args/args.dart';

import 'common_util.dart';
import 'io.dart';
import 'queries/index.dart' as queries;
import 'queries/index.dart';
import 'queries/source_lang.dart';
import 'reflect.dart';
import 'run_queries.dart';

/// Parse command line arguments
ParsedArgs parseArgs(List<String> args) {
  final parser = ArgParser(allowTrailingOptions: true)
    ..addSeparator('Core options:\n')
    ..addFlag('help', help: 'Give this help.', negatable: false)
    // The `--[no-]cache` parameter is a tri-state:
    //
    // fx codesize --cache => Always use cached bloaty reports
    // fx codesize --no-cache => Never use cached bloaty reports
    // fx codesize => If the system image is newer than the report index,
    //                re-run bloaty. Otherwise use cache. This is the default.
    ..addFlag(cache,
        help:
            'Use the cached version of the list of bloaty reports if available',
        defaultsTo: false,
        negatable: false)
    ..addFlag(noCache,
        help: 'Do not use the cache of the list of bloaty reports',
        defaultsTo: false,
        negatable: false)
    ..addOption(buildDir,
        help: 'The build output directory (e.g. out/default).\n'
            'If absent, defaults to the current output directory '
            '(FUCHSIA_BUILD_DIR)',
        defaultsTo: Platform.environment['FUCHSIA_BUILD_DIR'])
    ..addOption(concurrency,
        help: 'The number of worker threads.\n'
            'If absent, defaults to a reasonable number based on the CPU cores',
        abbr: 'j')
    ..addSeparator('Filtering by name and language:\n')
    ..addOption(fileRegex,
        help: 'Optionally specify a regex to only incorporate statistics\n'
            'from binaries whose name match the regex.',
        abbr: 'f',
        defaultsTo: '.*')
    ..addOption(onlyLang,
        help: 'Only incorporate statistics from binaries '
            'of this programming language.',
        abbr: 'l',
        allowed: SourceLang.values.map((e) => e.name))
    ..addSeparator('Filtering by run-time page-in frequency:\n')
    ..addOption(heatmap,
        help: 'Optionally specify a blob access heatmap to only show symbols '
            'that have never been used at run-time.\n'
            'The heatmap is a CSV in [merkle],[[frame index]:[frequency],...] '
            'format, where each frame is 8 KiB by default.\n'
            'See the detailed explanation of `cold_bytes_filter` in '
            'https://fuchsia.googlesource.com/third_party/bloaty/+/refs/heads/fuchsia/src/bloaty.proto\n'
            '')
    ..addOption(heatmapFrameSize,
        help: 'When a heatmap is used, specify the size of a frame in bytes',
        defaultsTo: '8192')
    ..addSeparator('Output controls:\n')
    ..addOption(outputFile,
        help: 'The destination for writing stats. If absent, assumes stdout.',
        abbr: 'o')
    ..addOption(format,
        help: 'Format for the output.',
        allowed: OutputFormat.values.map((e) => e.name),
        allowedHelp: <OutputFormat, String>{
          OutputFormat.terminal:
              'Character-based rendering suitable for terminal',
          OutputFormat.basic: 'Plain-text minimal summary',
          OutputFormat.html: 'Rich HTML formatting',
          OutputFormat.tsv: 'Tab-separated values table',
        }.map((key, value) => MapEntry<String, String>(key.name, value)),
        defaultsTo: OutputFormat.terminal.name);

  final queryHelp = StringBuffer();
  for (final query in queries.allQueries) {
    queryHelp
      ..write('      ${query.name.padRight(24)}')
      ..writeln(query.description);
    if (ReflectQuery.hasCustomArguments(query)) {
      for (final constructor in ReflectQuery.describeQueryConstructors(query)) {
        queryHelp.writeln('      $constructor  <-  optional arguments\n');
      }
    } else {
      queryHelp.writeln();
    }
  }
  queryHelp.write(r'''
The syntax for specifying optional arguments to queries is similar to Dart
function calls and passing arguments by name, with the exception that strings
and string-like argument values are un-quoted. For instance, to specify the
`sortBySize` argument to `DumpNames`, one could write on the command line:

      # Use single-quotes to escape any special characters on the shell.
      fx codesize 'DumpNames(sortBySize: true)'

''');

  const defaultQueryConstructors = <String>['CodeCategory', 'SourceLang'];
  final argResults = parser.parse(args);
  if (argResults['help']) {
    final examples =
        // ignore: prefer_interpolation_to_compose_strings
        ('# By default, codesize runs the ${defaultQueryConstructors.join(" and ")} signal\n'
                r'''
fx codesize

# Can run on a subset of binaries, filtered by a regex
# For instance, "(?!\[prebuilt\])" will skip all the prebuilts, which might be helpful
# if changing SDK libraries that won't lead to their update
fx codesize --file-regex '^(?!\[prebuilt\])'

# Another example, only looking at the ELF binaries in Zircon Boot Images
fx codesize --file-regex '^\[zbi: '

# Another example, only running on appmgr...
fx codesize --file-regex appmgr CodeCategory

# Dumping all symbols with annotation in a binary, also hiding unknown symbols
fx codesize --file-regex appmgr 'DumpSymbol(hideUnknown: true)'

# Look through all C++ symbols alongside their containing programs, sorted by aggregate size
fx codesize --only-lang=cpp 'UniqueSymbol(showCompileUnit: true, showProgram: true, hideUnknown: false)' | less
''')
            .split('\n')
            .map((s) => '      $s')
            .join('\n');
    print('Usage: fx codesize [OPTION]... [QUERY]...\n\n'
        'Looks at all the ELF binaries in the fvm/zbi images in the out dir,\n'
        'computes various bary size queries specified in [QUERY]...\n'
        'If [QUERY] is absent, defaults to CodeCategory and SourceLang\n\n'
        '${parser.usage}\n\n'
        'Supported queries:\n\n$queryHelp'
        'Some examples:\n\n$examples\n\n'
        '''Exit codes:

    0: Success
    1: General unhandled exception (indicates a bug in codesize)
    2: Known errors/unsatisfied preconditions (not a bug in codesize)
''');
    return null;
  }

  // Default query to `SourceLang` if none was set.
  List<String> queryConstructors = argResults.rest ?? [];
  if (queryConstructors.isEmpty) queryConstructors = defaultQueryConstructors;

  final List<QueryThunk> selectedQueries = queryConstructors
      .map((String s) => parseQueryConstructor(queriesByName, s))
      .toList(growable: false);

  // We close the sink in the main function.
  // ignore: close_sinks
  IOSink output = Io.get().out;
  if (argResults[outputFile] != null) {
    output = File(argResults[outputFile]).openWrite();
  }

  CachingBehavior cachingBehavior;
  if (argResults[cache] && argResults[noCache])
    throw Exception('--cache and --no-cache cannot both be specified');
  if (argResults[cache])
    cachingBehavior = CachingBehavior.alwaysUseCache;
  else if (argResults[noCache])
    cachingBehavior = CachingBehavior.neverUseCache;
  else
    cachingBehavior = CachingBehavior.useIfUpToDate;

  return ParsedArgs(
      buildDir: argResults[buildDir],
      fileRegex: RegExp(argResults[fileRegex]),
      output: output,
      cachingBehavior: cachingBehavior,
      // ignore: avoid_as
      concurrency: flatMap(argResults[concurrency] as String, int.parse),
      selectedQueries: List<QueryThunk>.from(selectedQueries),
      format: toOutputFormat(argResults[format]),
      // ignore: avoid_as
      onlyLang: flatMap(argResults[onlyLang] as String, toSourceLang),
      // ignore: avoid_as
      heatmap: flatMap(argResults[heatmap] as String, (x) => File(x)),
      heatmapFrameSize: int.parse(argResults[heatmapFrameSize]));
}

enum OutputFormat { terminal, basic, html, tsv }

extension on OutputFormat {
  String get name => removePrefix(toString(), 'OutputFormat.');
}

OutputFormat toOutputFormat(String name) {
  for (final format in OutputFormat.values) {
    if (format.name == name) return format;
  }
  return null;
}

extension on SourceLang {
  String get name => removePrefix(toString(), 'SourceLang.');
}

SourceLang toSourceLang(String name) {
  for (final lang in SourceLang.values) {
    if (lang.name == name) return lang;
  }
  return null;
}

enum CachingBehavior { alwaysUseCache, neverUseCache, useIfUpToDate }

final queriesByName = Map.fromEntries(
    allQueries.map((s) => MapEntry<String, QueryFactory>(s.name, s)));

/// Turns
///
/// ```
/// "foo: 5, bar: abc"
/// ```
///
/// into
///
/// ```
/// {'foo': '5', 'bar': 'abc'}
/// ```
Map<String, String> parseQueryConstructorArgs(String args) =>
    Map.fromEntries(args.split(',').map((s) {
      final tokens = s.trim().split(':');
      assert(tokens.length == 2);
      final name = tokens[0].trim();
      final value = tokens[1].trim();
      return MapEntry<String, String>(name, value);
    }));

/// Turns a string of the form `MyQuery(foo: 5, bar: 'abc')`
/// into a zero-arg function which when evaluated, produces
/// a new instance of `MyQuery` with those arguments.
QueryThunk parseQueryConstructor(
    Map<String, QueryFactory> queries, String _constructor) {
  final constructor = _constructor.trim();
  final startOfArgs = constructor.indexOf('(');
  String name;
  Map<String, String> args = {};
  if (startOfArgs == -1) {
    name = constructor;
  } else {
    name = constructor.substring(0, startOfArgs);
    if (constructor[constructor.length - 1] != ')') {
      throw Exception('$constructor should end with `)`');
    }
    args = parseQueryConstructorArgs(
        constructor.substring(startOfArgs + 1, constructor.length - 1));
  }
  QueryFactory f;
  if (queries.containsKey(name)) {
    f = queries[name];
  } else {
    throw Exception('Query `$name` not found. Pick from: ${queries.keys}');
  }
  return () => ReflectQuery.instantiate(f, args);
}

// Defining command line arguments ---------------------------------------------

const cache = 'cache';
const noCache = 'no-cache';
const buildDir = 'build-dir';
const fileRegex = 'file-regex';
const onlyLang = 'only-lang';
const outputFile = 'output';
const format = 'format';
const concurrency = 'concurrency';
const heatmap = 'heatmap';
const heatmapFrameSize = 'heatmap-frame-size';

class ParsedArgs {
  final CachingBehavior cachingBehavior;
  final String buildDir;
  final RegExp fileRegex;
  final SourceLang onlyLang;
  final IOSink output;
  final OutputFormat format;
  final int concurrency;
  final List<QueryThunk> selectedQueries;
  final File heatmap;
  final int heatmapFrameSize;

  ParsedArgs(
      {this.cachingBehavior,
      this.buildDir,
      this.fileRegex,
      this.onlyLang,
      this.output,
      this.format,
      this.concurrency,
      this.selectedQueries,
      this.heatmap,
      this.heatmapFrameSize});
}
