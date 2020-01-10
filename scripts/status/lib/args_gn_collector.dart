// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:status/status.dart';

class GNStatusParser {
  final RegExp argsImportExtractor = RegExp('([^/]*)/([^/]*)\.gni');

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

  List<Item> parseGn({ProcessResult processResult}) {
    List<Item> results = [];
    if (processResult.exitCode != 0) {
      // Ideally, in the line below, we would `throw Exception(...)` and catch it
      // likewise up top, thought that is proving hard to format correctly in the test
      // ignore: only_throw_errors
      throw 'Unexpected error running fx gn: exit code ${processResult.exitCode}\n---- stderr output:\n${processResult.stderr}\n------';
    } else {
      // removes any text preceeding the actual json string. Since gn outputs
      // the json string to stderr, any warning output by fx, like the metrics
      // warning, will get in the way of the json, causing a parsing error.
      String json = processResult.stderr;
      if (json.indexOf('{') > 0) {
        json = json.substring(json.indexOf('{'));
      }

      List<Map<String, dynamic>> argsTree =
          jsonDecode(json)['child'].cast<Map<String, dynamic>>().toList();

      BasicGnParser parser = BasicGnParser(argsTree);
      _addImportItems(parser, results);
      _addDirectItems(parser, results);
      _addCalculatedItems(parser, results);
      return results;
    }
  }

  void _addDirectItems(BasicGnParser parser, List<Item> appendTo) {
    for (String key in parser.assignedVariables.keys) {
      dynamic value = parser.assignedVariables[key];
      var title = gnTitles[key];
      if (title != null && (value is! List || value.isNotEmpty)) {
        // ignore: avoid_init_to_null
        var notes = null;
        if (title is List<String>) {
          title = gnTitles[key][0];
          notes = gnTitles[key][1];
        }
        appendTo.add(
            Item(CategoryType.buildInfo, key, title, value.toString(), notes));
      }
    }
  }

  void _addImportItems(BasicGnParser parser, List<Item> appendTo) {
    for (String importClause in parser.imports) {
      Match m = argsImportExtractor.firstMatch(importClause);
      if (m != null) {
        var key = m.group(1);
        var title = gnTitles[key] ?? key;
        appendTo.add(
          Item(CategoryType.buildInfo, key, title, m.group(2), importClause),
        );
      }
    }
  }

  void _addCalculatedItems(BasicGnParser parser, List<Item> appendTo) {
    // goma
    bool isGomaEnabled = parser.assignedVariables['use_goma'] == 'true';
    String gomaDir = parser.assignedVariables['goma_dir'];
    appendTo.add(Item(CategoryType.buildInfo, 'goma', 'Goma',
        isGomaEnabled ? 'enabled' : 'disabled', gomaDir));

    // release
    bool isRelease = parser.assignedVariables['is_debug'] == 'false';
    appendTo.add(Item(CategoryType.buildInfo, 'release', 'Is release?',
        isRelease ? 'true' : 'false', '--release argument of `fx set`'));
  }
}

class GNStatusChecker {
  Future<ProcessResult> checkGn({String pathToArgs}) async {
    pathToArgs ??= '${Platform.environment['FUCHSIA_BUILD_DIR']}/args.gn';
    return Process.run('fx', ['gn', 'format', '--dump-tree=json', pathToArgs]);
  }
}

class ArgsGnCollector implements Collector {
  @override
  Future<List<Item>> collect({
    GNStatusChecker statusChecker,
    GNStatusParser statusParser,
  }) async {
    statusChecker ??= GNStatusChecker();
    statusParser ??= GNStatusParser();

    ProcessResult pr = await statusChecker.checkGn();
    return statusParser.parseGn(processResult: pr);
  }
}
