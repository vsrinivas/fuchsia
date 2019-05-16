// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

import 'package:sl4f/sl4f.dart';

class MockSl4f extends Mock implements Sl4f {}

class MockDump extends Mock implements Dump {}

const String traceString = '{"traceEvents": [], "displayTimeUnit": "ns"}';
const String sl4fTraceRequestMethod = 'traceutil_facade.GetTraceFile';

void main(List<String> args) {
  // Verifies that [Traceutil.downloadTraceFile] requests a file from Sl4f and
  // writes its contents to a local file.
  test('download trace requests a file and writes it', () async {
    final mockDump = MockDump();
    final mockSl4f = MockSl4f();

    when(mockSl4f.request(sl4fTraceRequestMethod, any))
        .thenAnswer((_) => Future.value(traceString));

    final traceutil = Traceutil(mockSl4f, mockDump);

    await traceutil.downloadTraceFile('test-trace');

    // Verify Sl4f.request.
    final verifyMockSl4fRequest =
        verify(mockSl4f.request(sl4fTraceRequestMethod, captureAny))..called(1);
    expect(verifyMockSl4fRequest.captured.single,
        {'path': '/tmp/test-trace-trace.json'});

    // Verify Dump.writeAsBytes.
    final verifyMockDumpWriteAsBytes =
        verify(mockDump.writeAsBytes(captureAny, captureAny, captureAny))
          ..called(1);
    expect(verifyMockDumpWriteAsBytes.captured,
        ['test-trace-trace', 'json', utf8.encode(traceString)]);
  });
}
