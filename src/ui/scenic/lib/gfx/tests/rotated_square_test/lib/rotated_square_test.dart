// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// @dart = 2.8

import 'dart:collection' show SplayTreeMap;
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data' show Uint8List;

import 'package:collection/collection.dart' show DeepCollectionEquality;
import 'package:http/http.dart' as http;
import 'package:test/test.dart';

// Use a custom timeout rather than the test framework's timeout so that we can
// output a sensible failure message.
final Duration timeout = Duration(seconds: 15);

// TODO(rosswang): https://fuchsia-review.googlesource.com/c/fuchsia/+/258828

/// Wraps the error field of a JSON RPC as an [Exception].
class JsonRpcException implements Exception {
  JsonRpcException(this.error);
  final dynamic error;

  @override
  String toString() {
    return 'JSON RPC returned error: $error';
  }
}

class Sl4f {
  final _client = http.Client();
  String _ip;

  Sl4f() {
    _ip = Platform.environment['ip'];
    print('Target device IP: $_ip');
  }

  void close() {
    _client.close();
  }

  Future<dynamic> request(String method, [dynamic params]) async {
    final httpRequest = http.Request('GET', Uri.http(_ip, ''))
      ..body = jsonEncode({'id': '', 'method': method, 'params': params});

    final httpResponse =
        await http.Response.fromStream(await _client.send(httpRequest));
    final Map<String, dynamic> response = jsonDecode(httpResponse.body);
    final dynamic error = response['error'];
    if (error != null) {
      throw JsonRpcException(error);
    }

    return response['result'];
  }
}

class PixelTest {
  final Sl4f _sl4f = Sl4f();

  void close() {
    _sl4f.close();
  }

  Future<void> presentView(String url, [Map<String, dynamic> config]) async {
    await _sl4f
        .request('scenic_facade.PresentView', {'url': url, 'config': config});
  }

  // Returns the screenshot bytes, BGRA.
  Future<Uint8List> takeScreenshot() async {
    Map<String, dynamic> response =
        await _sl4f.request('scenic_facade.TakeScreenshot');
    Map<String, dynamic> info = response['info'];

    expect(info['pixel_format'], 'Bgra8');

    return base64Decode(response['data']);
  }
}

Future<bool> screenshotUntil(
    PixelTest pixelTest, bool Function(List<int>) condition) async {
  final Stopwatch stopwatch = Stopwatch()..start();
  while (stopwatch.elapsed < timeout) {
    try {
      if (condition(await pixelTest.takeScreenshot())) {
        return true;
      }
    } on JsonRpcException catch (e) {
      print(e);
    }
  }
  return false;
}

void main() {
  PixelTest pixelTest;

  setUp(() {
    pixelTest = PixelTest();
  });
  tearDown(() {
    pixelTest.close();
  });

  test('Rotated square renders correctly.', () async {
    // BGRA
    const int primaryColor = 0xb73a67ff;
    const int secondaryColor = 0x5700f5ff;

    await (pixelTest.presentView(
        'fuchsia-pkg://fuchsia.com/test_views#meta/rotated_square_view.cmx', {
      'intl_profile': {'temperature_unit': 'Fahrenheit'}
    }));

    // Order from most frequent to least frequent.
    final sortedHistogram = SplayTreeMap<int, Set<int>>(
        (count1, count2) => count2.compareTo(count1));

    final bool ok = await screenshotUntil(pixelTest, (List<int> bytes) {
      final Map<int, int> histogram = {}; // color => count
      for (int i = 0; i < bytes.length; i += 4) {
        final color = bytes[i] << 24 |
            bytes[i + 1] << 16 |
            bytes[i + 2] << 8 |
            bytes[i + 3];
        histogram[color] = (histogram[color] ?? 0) + 1;
      }

      sortedHistogram.clear();
      for (final entry in histogram.entries) {
        sortedHistogram.putIfAbsent(entry.value, () => {}).add(entry.key);
      }

      return DeepCollectionEquality().equals(sortedHistogram.values.take(2), [
        {primaryColor},
        {secondaryColor}
      ]);
    });

    expect(sortedHistogram.values.take(2), [
      {primaryColor},
      {secondaryColor}
    ]);
    assert(ok); // any failure should have been caught by expect
  });
}
