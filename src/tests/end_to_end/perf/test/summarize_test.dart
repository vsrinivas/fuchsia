// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:convert';
import 'dart:io';

import 'package:test/test.dart';

import 'summarize.dart';

void main() {
  test('mean', () {
    expect(mean([100, 101, 102, 103]), equals(101.5));
    expect(mean([123.45]), equals(123.45));
    expect(mean([123]), equals(123));
    // The int (non-double) type should be preserved for single-item inputs.
    expect(mean([123]), TypeMatcher<int>());
    expect(() {
      mean([]);
    }, throwsA(TypeMatcher<ArgumentError>()));
  });

  test('meanExcludingWarmup', () {
    expect(meanExcludingWarmup([999, 200, 201, 202, 203]), equals(201.5));
    expect(meanExcludingWarmup([999, 123]), equals(123));
    expect(meanExcludingWarmup([999.88]), equals(999.88));
    expect(meanExcludingWarmup([999]), equals(999));
    // The int (non-double) type should be preserved for single-item inputs.
    expect(meanExcludingWarmup([999]), TypeMatcher<int>());
    expect(() {
      meanExcludingWarmup([]);
    }, throwsA(TypeMatcher<ArgumentError>()));
  });

  File writeTempFile(String contents) {
    final tempDir = Directory.systemTemp.createTempSync();
    addTearDown(() {
      tempDir.deleteSync(recursive: true);
    });
    return File('${tempDir.path}/temp_file')..writeAsStringSync(contents);
  }

  // Test summarizeFuchsiaPerfFiles() on a simple case where we have
  // one set of results (corresponding to a single process run) of a
  // single test case.
  test('summarize_results_simple_case', () {
    final files = [
      writeTempFile(jsonEncode([
        {
          'label': 'test1',
          'test_suite': 'suite1',
          'unit': 'nanoseconds',
          // This gives a meanExcludingWarmup of 103.
          'values': [200, 101, 102, 103, 104, 105],
        }
      ])),
    ];
    final output = summarizeFuchsiaPerfFiles(files);
    expect(
        output,
        equals([
          {
            'label': 'test1',
            'test_suite': 'suite1',
            'unit': 'nanoseconds',
            'values': [103.0],
          }
        ]));
  });

  // Test summarizeFuchsiaPerfFiles() on a more complex case: We have
  // multiple entries (corresponding to multiple process runs), and we
  // take the mean of each entry's meanExcludingWarmup.
  test('summarize_results_mean_of_means', () {
    final files = [
      writeTempFile(jsonEncode([
        {
          'label': 'test1',
          'test_suite': 'suite1',
          'unit': 'nanoseconds',
          // This gives a meanExcludingWarmup of 110.
          'values': [2000, 108, 109, 110, 111, 112],
        }
      ])),
      writeTempFile(jsonEncode([
        {
          'label': 'test1',
          'test_suite': 'suite1',
          'unit': 'nanoseconds',
          // This gives a meanExcludingWarmup of 210.
          'values': [3000, 207, 208, 209, 210, 211, 212, 213],
        }
      ])),
    ];
    final output = summarizeFuchsiaPerfFiles(files);
    expect(
        output,
        equals([
          {
            'label': 'test1',
            'test_suite': 'suite1',
            'unit': 'nanoseconds',
            // This expected value is the mean of [110, 210].
            'values': [160.0],
          }
        ]));
  });

  // Test that summarizeFuchsiaPerfFiles() works with fractional
  // values.  Use test values that can be represented exactly by
  // floating point numbers.
  test('summarize_results_fractions', () {
    final files = [
      writeTempFile(jsonEncode([
        {
          'label': 'test1',
          'test_suite': 'suite1',
          'unit': 'nanoseconds',
          // This gives a meanExcludingWarmup of 2 / 256.
          'values': [5000, 1 / 256, 3 / 256],
        },
        {
          'label': 'test1',
          'test_suite': 'suite1',
          'unit': 'nanoseconds',
          // This gives a meanExcludingWarmup of 6 / 256.
          'values': [5000, 5 / 256, 7 / 256],
        },
      ])),
    ];
    final output = summarizeFuchsiaPerfFiles(files);
    expect(
        output,
        equals([
          {
            'label': 'test1',
            'test_suite': 'suite1',
            'unit': 'nanoseconds',
            'values': [4 / 256],
          }
        ]));
  });

  // Check that JSON data written to a file using
  // writeFuchsiaPerfJson() can be read back successfully and give the
  // same value.
  test('writeFuchsiaPerfJson_round_trip', () async {
    final dynamic jsonData = [
      {
        'foo': 'bar',
        'list': [1, 2, 3],
      },
      {
        'foo': 'bar2',
        'list': [4, 5, 6],
      },
      'string',
      [4, 5, 6],
    ];
    final File file = writeTempFile('');
    await writeFuchsiaPerfJson(file, jsonData);
    expect(jsonDecode(file.readAsStringSync()), equals(jsonData));
  });

  // Check that writeFuchsiaPerfJson() outputs a newline after each
  // top-level entry.
  test('writeFuchsiaPerfJson_newlines', () async {
    final File file = writeTempFile('');
    await writeFuchsiaPerfJson(file, ['foo', 'bar', 'qux']);
    expect(file.readAsStringSync(), '["foo",\n"bar",\n"qux"]\n');
  });
}
