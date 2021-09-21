// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fidl_fuchsia_modular/fidl_async.dart' as modular;
import 'package:fidl_fuchsia_modular_testing/fidl_async.dart';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:fuchsia_modular_testing/test.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

const Pattern _isolatePattern = 'slider_mod.cmx';
const _testAppUrl = 'fuchsia-pkg://fuchsia.com/slider_mod#meta/slider_mod.cmx';

final _addModCommand = modular.AddMod(
    modName: [_isolatePattern],
    modNameTransitional: 'root',
    intent: modular.Intent(action: 'action', handler: _testAppUrl),
    surfaceRelation: modular.SurfaceRelation());

Future<void> _launchModUnderTest(TestHarnessProxy testHarness) async {
  final puppetMaster = modular.PuppetMasterProxy();
  await testHarness.connectToModularService(
      ModularService.withPuppetMaster(puppetMaster.ctrl.request()));

  // Use PuppetMaster to start a fake story and launch the mod under test
  final storyPuppetMaster = modular.StoryPuppetMasterProxy();
  await puppetMaster.controlStory(
      'slider_mod_test', storyPuppetMaster.ctrl.request());
  await storyPuppetMaster
      .enqueue([modular.StoryCommand.withAddMod(_addModCommand)]);
  await storyPuppetMaster.execute();
}

void main() {
  group('slider_mod tests', () {
    TestHarnessProxy testHarness;
    FlutterDriver driver;

    setUp(() async {
      testHarness = await launchTestHarness();

      await testHarness.run(
        TestHarnessSpec(
          envServices: EnvironmentServicesSpec(
            serviceDir: Channel.fromFile('/svc'),
          ),
        ),
      );
      await _launchModUnderTest(testHarness);

      driver = await FlutterDriver.connect(
          fuchsiaModuleTarget: _isolatePattern,
          printCommunication: true,
          logCommunicationToFile: false);
    });

    tearDown(() async {
      await driver?.close();
      testHarness.ctrl.close();
    });

    test(
        'Verify the agent is connected and replies with the correct Fibonacci '
        'result', () async {
      print('tapping on Calc Fibonacci button');
      await driver.tap(find.text('Calc Fibonacci'));
      print('verifying the result');
      await driver.waitFor(find.byValueKey('fib-result-widget-key'));
      print('test is finished successfully');
    });
  }, skip: 'disabled due to flake. See (34885) for more information');
}
