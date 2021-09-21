// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// ignore_for_file: directives_ordering
import 'dart:async';
import 'package:fidl_fuchsia_examples_inspect/fidl_async.dart' as fidl_codelab;
import 'package:test/test.dart';
import 'package:inspect_codelab_shared/codelab_environment.dart';

// [START include_test_stuff]
// NOTE: this test is currently commented out in the BUILD.gn file.
// TODO(fxb/45831): re-enable
import 'dart:convert';
import 'package:fidl_fuchsia_diagnostics/fidl_async.dart';
import 'package:fidl_fuchsia_mem/fidl_async.dart';
import 'package:fuchsia_services/services.dart';
import 'package:zircon/zircon.dart';
// [END include_test_stuff]

void main() {
  CodelabEnvironment env;
  const serverName = 'inspect-dart-codelab-part-5';

  Future<fidl_codelab.ReverserProxy> startComponentAndConnect({
    bool includeFizzbuzz = false,
  }) async {
    if (includeFizzbuzz) {
      await env.startFizzBuzz();
    }

    const reverserUrl =
        'fuchsia-pkg://fuchsia.com/$serverName#meta/$serverName.cmx';
    return await env.startReverser(reverserUrl);
  }

  // [START get_inspect]
  String readBuffer(Buffer buffer) {
    final dataVmo = SizedVmo(buffer.vmo.handle, buffer.size);
    final data = dataVmo.read(buffer.size);
    return utf8.decode(data.bytesAsUint8List());
  }

  Future<Map<String, dynamic>> getInspectHierarchy() async {
    final archive = ArchiveAccessorProxy();
    final incoming = Incoming.fromSvcPath()..connectToService(archive);

    final params = StreamParameters(
      dataType: DataType.inspect,
      streamMode: StreamMode.snapshot,
      format: Format.json,
      clientSelectorConfiguration: ClientSelectorConfiguration.withSelectors([
        SelectorArgument.withRawSelector('${env.label}/$serverName.cmx:root'),
      ]),
    );

    // ignore: literal_only_boolean_expressions
    while (true) {
      final iterator = BatchIteratorProxy();
      await archive.streamDiagnostics(params, iterator.ctrl.request());
      final batch = await iterator.getNext();
      for (final entry in batch) {
        final jsonData = readBuffer(entry.json);
        if (jsonData.contains('fuchsia.inspect.Health') &&
            !jsonData.contains('STARTING_UP')) {
          await incoming.close();
          return json.decode(jsonData);
        }
      }
      iterator.ctrl.close();
      await Future.delayed(Duration(milliseconds: 150));
    }
  }
  // [END get_inspect]

  setUp(() async {
    env = CodelabEnvironment();
    await env.create();
  });

  tearDown(() async {
    env.dispose();
  });

  test('start with fizzbuzz', () async {
    final reverser = await startComponentAndConnect(includeFizzbuzz: true);
    final result = await reverser.reverse('hello');
    expect(result, equals('olleh'));

    // [START result_hierarchy]
    final inspectData = await getInspectHierarchy();
    // [END result_hierarchy]
    expect(inspectData['payload']['root']['fuchsia.inspect.Health']['status'],
        'OK');

    reverser.ctrl.close();
  });

  test('start without fizzbuzz', () async {
    final reverser = await startComponentAndConnect(includeFizzbuzz: false);
    final result = await reverser.reverse('hello');
    expect(result, equals('olleh'));

    final inspectData = await getInspectHierarchy();
    final healthNode = inspectData['payload']['root']['fuchsia.inspect.Health'];
    expect(healthNode['status'], 'UNHEALTHY');
    expect(healthNode['message'], 'FizzBuzz connection closed');

    reverser.ctrl.close();
  });
}
