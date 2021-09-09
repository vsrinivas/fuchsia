// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'collector.dart';

/// Receives a [ProcessResult] which contains information about Jiri overrides.
class JiriOverrideParser {
  bool parse({ProcessResult processResult}) {
    if (processResult.exitCode != 0) {
      return false;
    }
    LineSplitter ls = LineSplitter();
    List<String> lines = ls.convert(processResult.stdout);

    return lines.isNotEmpty;
  }
}

/// Asks Jiri for a list of overrides. Intended to be paired with the
/// [JiriOverrideParser] class, which knows how to make sense of this result.
class JiriOverrideChecker {
  Future<ProcessResult> checkStatus() async {
    return Process.run('jiri', [
      'override',
      '-list',
    ]);
  }
}

/// Confirms whether there are any Jiri overrides configured.
///
/// Accepts optional [JiriOverrideChecker] and [JiriOverrideParser] helper
/// classes which respectively know how to make a Jiri system call and
/// make sense of those results. Not passing values leads to default
/// behavior, which is usually what you want.
class JiriCollector implements Collector {
  @override
  Future<List<Item>> collect({
    JiriOverrideChecker jiriOverrideChecker,
    JiriOverrideParser jiriOverrideParser,
  }) async {
    // Create default values
    jiriOverrideChecker ??= JiriOverrideChecker();
    jiriOverrideParser ??= JiriOverrideParser();

    ProcessResult pr = await jiriOverrideChecker.checkStatus();
    bool jiriOverrides = jiriOverrideParser.parse(processResult: pr);

    return <Item>[
      Item(
        CategoryType.sourceInfo,
        'has_jiri_overrides',
        'Has Jiri overrides?',
        jiriOverrides,
        'output of \'jiri override -list\'',
      ),
    ];
  }
}
