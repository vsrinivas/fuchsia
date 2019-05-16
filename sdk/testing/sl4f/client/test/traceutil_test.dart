// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

import 'package:sl4f/sl4f.dart';

class MockSl4f extends Mock implements Sl4f {}

class MockDump extends Mock implements Dump {}

const String sl4fTraceRequestMethod = 'traceutil_facade.GetTraceFile';

void main(List<String> args) {
  // Verifies that [Traceutil.downloadTraceFile] requests a file from Sl4f and
  // writes its contents to a local file.
  test('download trace requests a file and writes it', () async {
    final List<int> traceData = utf8.encode('{"traceEvents": [], "displayTimeUnit": "ns"}');
    final mockDump = MockDump();
    final mockSl4f = MockSl4f();

    when(mockSl4f.request(sl4fTraceRequestMethod, any)).thenAnswer(
        (_) => Future.value(base64.encode(traceData)));

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
        ['test-trace-trace', 'json', traceData]);
  });

  test('non-utf8 data', () async {
    final List<int> nonUtf8Data = [0x1f, 0x8b, 0x08, 0x08, 0x35, 0xdc, 0x00, 0x03];

    expect(() => utf8.decode(nonUtf8Data), throwsException);

    final mockDump = MockDump();
    final mockSl4f = MockSl4f();

    when(mockSl4f.request(sl4fTraceRequestMethod, any)).thenAnswer(
        (_) => Future.value(base64.encode(nonUtf8Data)));

    final traceutil = Traceutil(mockSl4f, mockDump);

    await traceutil.downloadTraceFile('non-utf8');

    final verifyMockSl4fRequest =
        verify(mockSl4f.request(sl4fTraceRequestMethod, captureAny))..called(1);
    expect(verifyMockSl4fRequest.captured.single,
        {'path': '/tmp/non-utf8-trace.json'});

    final verifyMockDumpWriteAsBytes =
        verify(mockDump.writeAsBytes(captureAny, captureAny, captureAny))
          ..called(1);
    expect(verifyMockDumpWriteAsBytes.captured,
        ['non-utf8-trace', 'json', nonUtf8Data]);
  });
}
