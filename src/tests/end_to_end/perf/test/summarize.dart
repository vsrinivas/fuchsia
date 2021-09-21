// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:convert';
import 'dart:io';

import 'package:tuple/tuple.dart';

num mean(List values) {
  if (values.isEmpty) {
    throw ArgumentError('Cannot calculate the mean for an empty list');
  }
  // If the list contains just a single value, preserve the value's
  // type (int vs. double).
  if (values.length == 1) {
    return values[0];
  }
  double sum = 0;
  for (final value in values) {
    sum += value;
  }
  return sum / values.length;
}

num meanExcludingWarmup(List values) {
  if (values.isEmpty) {
    throw ArgumentError('Cannot calculate the mean for an empty list');
  }
  if (values.length == 1) {
    return values[0];
  }
  double sum = 0;
  for (int index = 1; index < values.length; index++) {
    sum += values[index];
  }
  return sum / (values.length - 1);
}

// This function takes a set of "raw data" fuchsiaperf files as input
// and produces a "summary" fuchsiaperf file as output.  The output
// contains just the average values for each test case.
//
// This summarization does two things that are worth calling out:
//
//  * We treat the first value in each fuchsiaperf entry as a warmup
//    run and drop it.
//  * There may be multiple entries for the same test case (for
//    multiple process runs), in which case we merge them.
//
// Doing this as a postprocessing step in Dart has these benefits:
//
//  * It avoids the need to implement this processing either upstream
//    (in the C++ perftest library or in similar libraries for other
//    languages) or in downstream consumers.
//  * The summary fuchsiaperf file is much smaller than the "raw data"
//    fuchsiaperf files and hence more manageable.
//  * The "raw data" fuchsiaperf files can still be made available for
//    anyone who wishes to analyse the raw data.
dynamic summarizeFuchsiaPerfFiles(List<File> jsonFiles) {
  final Map<Tuple2<String, String>, dynamic> outputByName = {};
  final List outputList = [];

  for (final File jsonFile in jsonFiles) {
    final jsonData = jsonDecode(jsonFile.readAsStringSync());
    if (!(jsonData is List)) {
      throw ArgumentError('Top level fuchsiaperf node should be a list');
    }
    for (final Map<String, dynamic> entry in jsonData) {
      final fullName =
          Tuple2<String, String>(entry['test_suite'], entry['label']);
      dynamic outputEntry = outputByName[fullName];
      if (outputEntry == null) {
        outputEntry = {
          'label': entry['label'],
          'test_suite': entry['test_suite'],
          'unit': entry['unit'],
          'values': [],
        };
        outputByName[fullName] = outputEntry;
        outputList.add(outputEntry);
      } else {
        if (entry['unit'] != outputEntry['unit']) {
          throw ArgumentError('Inconsistent units in fuchsiaperf results: '
              '${entry['unit']}" and "${outputEntry['unit']}" '
              'for test "${entry['test_suite']}", "${entry['label']}"');
        }
      }

      outputEntry['values'].add(meanExcludingWarmup(entry['values']));
    }
  }

  for (final entry in outputList) {
    entry['values'] = [mean(entry['values'])];
  }

  return outputList;
}

// Write fuchsiaperf.json data to a file with one top-level entry per
// line, which is usually the right amount to make the file
// human-readable.  Doing full pretty-printing tends to make the file
// less human-readable and also makes the file larger unnecessarily.
Future<void> writeFuchsiaPerfJson(File destinationFile, List json) async {
  final ioSink = destinationFile.openWrite()..write('[');
  bool isFirst = true;
  for (final entry in json) {
    if (!isFirst) {
      ioSink.write(',\n');
    }
    ioSink.write(jsonEncode(entry));
    isFirst = false;
  }
  ioSink.write(']\n');
  await ioSink.close();
}
