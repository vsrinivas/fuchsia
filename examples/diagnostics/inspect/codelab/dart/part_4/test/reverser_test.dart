// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fuchsia_inspect/inspect.dart' as inspect;
import 'package:fuchsia_inspect/testing.dart';
import 'package:fuchsia_services/services.dart';
import 'package:inspect_dart_codelab_part_4_lib/reverser.dart';
import 'package:test/test.dart';

void main() {
  FakeVmoHolder vmo;
  inspect.Inspect inspector;

  ReverserImpl openReverser(
    inspect.Node node,
    inspect.IntProperty globalRequestCount,
  ) {
    return ReverserImpl(ReverserStats(node, globalRequestCount));
  }

  setUpAll(() {
    final context = ComponentContext.createAndServe();
    vmo = FakeVmoHolder(256 * 1024);
    inspector = inspect.Inspect.forTesting(vmo)..serve(context.outgoing);
  });

  // [START reverser_test]
  test('reverser', () async {
    // [START_EXCLUDE]
    final node = inspector.root.child('reverser_service');
    final globalRequestCount = node.intProperty('total_requests')..setValue(0);

    final reverser1 =
        openReverser(node.child('connection0'), globalRequestCount);
    final reverser2 =
        openReverser(node.child('connection1'), globalRequestCount);

    final result1 = await reverser1.reverse('hello');
    expect(result1, equals('olleh'));

    final result2 = await reverser1.reverse('world');
    expect(result2, equals('dlrow'));

    final result3 = await reverser2.reverse('another');
    expect(result3, equals('rehtona'));
    // [END EXCLUDE]

    final matcher = VmoMatcher(vmo);

    var reverserServiceNode = matcher.node().at(['reverser_service']);
    expect(
        reverserServiceNode.propertyEquals('total_requests', 3), hasNoErrors);
    expect(
        reverserServiceNode.at(['connection0'])
          ..propertyEquals('request_count', 2)
          ..propertyEquals('response_count', 2),
        hasNoErrors);
    // [END reverser_test]
    expect(
        reverserServiceNode.at(['connection1'])
          ..propertyEquals('request_count', 1)
          ..propertyEquals('response_count', 1),
        hasNoErrors);

    reverser1.dispose();

    reverserServiceNode = matcher.node().at(['reverser_service']);
    expect(
        reverserServiceNode.propertyEquals('total_requests', 3), hasNoErrors);
    expect(reverserServiceNode..missingChild('connection0'), hasNoErrors);
    expect(
        reverserServiceNode.at(['connection1'])
          ..propertyEquals('request_count', 1)
          ..propertyEquals('response_count', 1),
        hasNoErrors);
  });
}
