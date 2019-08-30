// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'gn_parser.dart';
import 'collector.dart';

class ArgsGnCollector implements Collector {
  final RegExp ARGS_IMPORT_EXTRACTOR = RegExp('([^/]*)/([^/]*)\.gni');

  Map gnTitles = {
    'boards': 'Board',
    'products': 'Product',
    'universe_package_labels': [
      'Universe packages',
      '--with argument of `fx set`'
    ],
    'base_package_labels': [
      'Base packages',
      '--with-base argument of `fx set`'
    ],
  };

  @override
  FutureOr<List<Item>> collect(bool includeSlow,
      {List<Category> restrictCategories}) {
    var args_file = Platform.environment['FUCHSIA_BUILD_DIR'] + '/args.gn';
    Future<List<Item>> gnParserRunner =
        Process.run('fx', ['gn', 'format', '--dump-tree=json', args_file])
            .then((ProcessResult pr) {
      List<Item> result = new List();
      if (pr.exitCode != 0) {
        throw ('Unexpected error running fx gn: exit code ${pr.exitCode}\n---- stderr output:\n${pr.stderr}\n------');
      } else {
        var argsTree = jsonDecode(pr.stderr)['child'];
        // TO DEBUG:
        // stderr.writeln(jsonEncode(argsTree));
        BasicGnParser parser = new BasicGnParser(argsTree);
        _addImportItems(parser, result);
        _addDirectItems(parser, result);
        _addCalculatedItems(parser, result);
        return result;
      }
    });
    return gnParserRunner;
  }

  void _addDirectItems(BasicGnParser parser, List<Item> appendTo) {
    for (String key in parser.assignedVariables.keys) {
      dynamic value = parser.assignedVariables[key];
      var title = gnTitles[key];
      if (title != null && (value is! List || value.isNotEmpty)) {
        var notes = null;
        if (title is List<String>) {
          title = gnTitles[key][0];
          notes = gnTitles[key][1];
        }
        appendTo.add(new Item(
            CategoryType.buildInfo, key, title, value.toString(), notes));
      }
    }
  }

  void _addImportItems(BasicGnParser parser, List<Item> appendTo) {
    for (String importClause in parser.imports) {
      Match m = ARGS_IMPORT_EXTRACTOR.firstMatch(importClause);
      if (m != null) {
        var key = m.group(1);
        var title = gnTitles[key] ?? key;
        appendTo.add(new Item(
            CategoryType.buildInfo, key, title, m.group(2), importClause));
      }
    }
  }

  void _addCalculatedItems(BasicGnParser parser, List<Item> appendTo) {
    // goma
    bool isGomaEnabled = parser.assignedVariables['use_goma'] == 'true';
    String gomaDir = parser.assignedVariables['goma_dir'];
    appendTo.add(new Item(CategoryType.buildInfo, 'goma', 'Goma',
        isGomaEnabled ? 'enabled' : 'disabled', gomaDir));

    // release
    bool isRelease = parser.assignedVariables['is_debug'] == 'false';
    appendTo.add(new Item(CategoryType.buildInfo, 'release', 'Is release?',
        isRelease ? 'true' : 'false', '--release argument of `fx set`'));
  }
}
