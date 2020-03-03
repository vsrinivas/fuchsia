// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

import 'package:sl4f/sl4f.dart';

class MockSl4f extends Mock implements Sl4f {}

void main(List<String> args) {
  test('Can return test result without steps', () async {
    TestResult expected = TestResult()
      ..duration_ms = 2
      ..outcome = 'passed'
      ..primary_log_path = 'log_path';
    final sl4f = MockSl4f();
    when(sl4f.request('test_facade.RunTest', any)).thenAnswer((_) =>
        Future.value({
          'outcome': 'passed',
          'duration_ms': 2,
          'primary_log_path': 'log_path'
        }));

    final TestResult result = await Test(sl4f).runTest('some_test');
    expect(result.toString(), expected.toString());
  });

  test('Can return test result with steps', () async {
    TestResult expected = TestResult()
      ..duration_ms = 2
      ..outcome = 'passed'
      ..primary_log_path = 'log_path'
      ..successful_completion = true
      ..steps = [
        TestStep()
          ..name = 'name1'
          ..duration_ms = 1
          ..outcome = 'passed'
          ..primary_log_path = 'log_path1'
          ..artifacts = {'key1': 'value1', 'key2': 'value2', 'key3': 4},
        TestStep()
          ..name = 'name2'
          ..duration_ms = 1
          ..outcome = 'passed'
          ..primary_log_path = 'log_path2'
      ];

    final sl4f = MockSl4f();
    when(sl4f.request('test_facade.RunTest', any))
        .thenAnswer((_) => Future.value({
              'outcome': 'passed',
              'duration_ms': 2,
              'primary_log_path': 'log_path',
              'steps': [
                {
                  'name': 'name1',
                  'outcome': 'passed',
                  'duration_ms': 1,
                  'primary_log_path': 'log_path1',
                  'artifacts': {'key1': 'value1', 'key2': 'value2', 'key3': 4}
                },
                {
                  'name': 'name2',
                  'outcome': 'passed',
                  'duration_ms': 1,
                  'primary_log_path': 'log_path2',
                }
              ]
            }));

    final TestResult result = await Test(sl4f).runTest('some_test');
    expect(result.toString(), expected.toString());
  });
}
