// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_modular/fidl_async.dart' as fidl_modular;
import 'package:fidl_fuchsia_modular_testing/fidl_async.dart' as fidl_testing;
import 'package:fuchsia_logger/logger.dart';
// ignore_for_file: implementation_imports
import 'package:fuchsia_modular_testing/src/test_harness_fixtures.dart';
import 'package:test/test.dart';

void main() {
  setupLogger();

  group('harness launching', () {
    late fidl_testing.TestHarnessProxy harness;

    setUp(() async {
      harness = await launchTestHarness();
    });

    tearDown(() {
      harness.ctrl.close();
    });

    test('launch harness can control story in hermetic environment', () async {
      const url = 'fuchsia-pkg://example.com/foo#meta/foo.cmx';
      await harness.run(fidl_testing.TestHarnessSpec(
        componentsToIntercept: [fidl_testing.InterceptSpec(componentUrl: url)],
      ));

      expect(harness.onNewComponent,
          emits((v) => v.startupInfo.launchInfo.url == url));

      final puppetMaster = fidl_modular.PuppetMasterProxy();

      await harness.connectToModularService(
          fidl_testing.ModularService.withPuppetMaster(
              puppetMaster.ctrl.request()));

      final storyPuppetMaster = fidl_modular.StoryPuppetMasterProxy();
      await puppetMaster.controlStory('foo', storyPuppetMaster.ctrl.request());
      puppetMaster.ctrl.close();

      final addMod = fidl_modular.AddMod(
        intent: fidl_modular.Intent(action: '', handler: url),
        surfaceParentModName: [],
        modName: ['mod'],
        surfaceRelation: fidl_modular.SurfaceRelation(),
      );

      await storyPuppetMaster
          .enqueue([fidl_modular.StoryCommand.withAddMod(addMod)]);
      await storyPuppetMaster.execute();
      storyPuppetMaster.ctrl.close();
    });
  });
}
