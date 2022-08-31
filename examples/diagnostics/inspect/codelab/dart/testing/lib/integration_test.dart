// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: prefer_initializing_formals, unnecessary_this, always_declare_return_types, type_annotate_public_apis, unnecessary_brace_in_string_interps, unused_import

import 'dart:async';
import 'package:fidl_fuchsia_examples_inspect/fidl_async.dart' as fidl_codelab;
import 'package:fuchsia_component_test/realm_builder.dart';

// [START include_test_stuff]
import 'package:fuchsia_inspect/reader.dart';
// [END include_test_stuff]

const fizzbuzzUrl = '#meta/fizzbuzz.cm';

class IntegrationTest {
  RealmInstance instance;
  int part;

  IntegrationTest._create(RealmInstance instance, int part) {
    this.instance = instance;
    this.part = part;
  }

  static Future<IntegrationTest> create(
    int part, {
    bool includeFizzBuzz = true,
  }) async {
    final builder = await RealmBuilder.create();
    final reverser = await builder.addChild(
      'part_${part}',
      '#meta/part_${part}.cm',
      ChildOptions(),
    );
    if (includeFizzBuzz) {
      final fizzbuzz =
          await builder.addChild('fizzbuzz', fizzbuzzUrl, ChildOptions());
      await builder.addRoute(Route()
        ..capability(ProtocolCapability(fidl_codelab.FizzBuzz.$serviceName))
        ..from(Ref.child(fizzbuzz))
        ..to(Ref.child(reverser)));
      await builder.addRoute(Route()
        ..capability(ProtocolCapability('fuchsia.logger.LogSink'))
        ..from(Ref.parent())
        ..to(Ref.child(fizzbuzz)));
    }
    await builder.addRoute(Route()
      ..capability(ProtocolCapability(fidl_codelab.Reverser.$serviceName))
      ..from(Ref.child(reverser))
      ..to(Ref.parent()));
    await builder.addRoute(Route()
      ..capability(ProtocolCapability('fuchsia.logger.LogSink'))
      ..from(Ref.parent())
      ..to(Ref.child(reverser)));
    return IntegrationTest._create(await builder.build(), part);
  }

  fidl_codelab.ReverserProxy connectToReverser() {
    final reverser = fidl_codelab.ReverserProxy();
    this.instance.root.connectToNamedProtocolAtExposedDir(
          fidl_codelab.Reverser.$serviceName,
          reverser.ctrl.request().passChannel(),
        );
    return reverser;
  }

  get reverserMonikerForSelectors =>
      'realm_builder\\:${this.instance.root.childName}/part_${this.part}';

  Future<List<DiagnosticsData<InspectMetadata>>> getReverserInspect() async {
    final reverserMoniker = this.reverserMonikerForSelectors;
    // [START get_inspect]
    final reader = ArchiveReader.forInspect(
        selectors: [Selector.fromRawSelector('${reverserMoniker}:root')]);
    final snapshot = await reader.snapshot(
        acceptSnapshot: (snapshot) =>
            snapshot.isNotEmpty &&
            snapshot[0].payload['root']['fuchsia.inspect.Health']['status'] !=
                'STARTING_UP');
    // [END get_inspect]
    return snapshot;
  }

  void dispose() {
    this.instance.root.close();
  }
}
