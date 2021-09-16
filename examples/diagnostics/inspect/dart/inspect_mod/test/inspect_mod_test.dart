// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'dart:io' as dartio;
import 'dart:typed_data';

import 'package:fidl_fuchsia_modular/fidl_async.dart';
import 'package:fidl_fuchsia_modular_testing/fidl_async.dart';
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:fuchsia_inspect/testing.dart';
import 'package:fuchsia_remote_debug_protocol/logging.dart';
import 'package:fuchsia_services/services.dart';
import 'package:glob/glob.dart';
import 'package:glob/list_local_fs.dart';
import 'package:test/test.dart';

const Pattern _testAppName = 'inspect_mod.cmx';
const _testAppUrl = 'fuchsia-pkg://fuchsia.com/inspect_mod#meta/$_testAppName';
const _modularTestHarnessURL =
    'fuchsia-pkg://fuchsia.com/modular_test_harness#meta/modular_test_harness.cmx';

TestHarnessProxy testHarnessProxy = TestHarnessProxy();
ComponentControllerProxy testHarnessController = ComponentControllerProxy();

// TODO(fxbug.dev/4487) Replace the test-harness / launch-mod boilerplate when possible.
// Starts Modular TestHarness with dev shells. This should be called from within
// a try/finally or similar construct that closes the component controller.
Future<void> _startTestHarness() async {
  final launcher = LauncherProxy();
  final incoming = Incoming();

  // launch TestHarness component
  final svc = Incoming.fromSvcPath()..connectToService(launcher);
  await launcher.createComponent(
      LaunchInfo(
          url: _modularTestHarnessURL,
          directoryRequest: incoming.request().passChannel()),
      testHarnessController.ctrl.request());

  // connect to TestHarness service
  incoming.connectToService(testHarnessProxy);

  // helper function to convert a map of service to url into list of
  // [ComponentService]
  List<ComponentService> _toComponentServices(
      Map<String, String> serviceToUrl) {
    final componentServices = <ComponentService>[];
    for (final svcName in serviceToUrl.keys) {
      componentServices
          .add(ComponentService(name: svcName, url: serviceToUrl[svcName]));
    }
    return componentServices;
  }

  final testHarnessSpec = TestHarnessSpec(
      envServicesToInherit: [
        'fuchsia.net.name.Lookup',
        'fuchsia.posix.socket.Provider',
        'fuchsia.vulkan.loader.Loader',
      ],
      envServices: EnvironmentServicesSpec(
          servicesFromComponents: _toComponentServices({
        'fuchsia.devicesettings.DeviceSettingsManager':
            'fuchsia-pkg://fuchsia.com/device_settings_manager#meta/device_settings_manager.cmx',
        'fuchsia.fonts.Provider':
            'fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx',
        'fuchsia.identity.account.AccountManager':
            'fuchsia-pkg://fuchsia.com/account_manager#meta/account_manager.cmx',
        'fuchsia.sysmem.Allocator':
            'fuchsia-pkg://fuchsia.com/sysmem_connector#meta/sysmem_connector.cmx',
        'fuchsia.tracelink.Registry':
            'fuchsia-pkg://fuchsia.com/trace_manager#meta/trace_manager.cmx',
        'fuchsia.ui.input.ImeService':
            'fuchsia-pkg://fuchsia.com/text_manager#meta/text_manager.cmx',
        'fuchsia.ui.policy.Presenter':
            'fuchsia-pkg://fuchsia.com/root_presenter#meta/root_presenter.cmx',
        'fuchsia.ui.scenic.Scenic':
            'fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx',
      })));

  // run the test harness which will create an encapsulated test env
  await testHarnessProxy.run(testHarnessSpec);
  await svc.close();
}

Future<void> _launchModUnderTest() async {
  // get the puppetMaster service from the encapsulated test env
  final puppetMasterProxy = PuppetMasterProxy();
  await testHarnessProxy.connectToModularService(
      ModularService.withPuppetMaster(puppetMasterProxy.ctrl.request()));
  // use puppetMaster to start a fake story an launch the mod under test
  final storyPuppetMasterProxy = StoryPuppetMasterProxy();
  await puppetMasterProxy.controlStory(
      'fooStoryName', storyPuppetMasterProxy.ctrl.request());
  await storyPuppetMasterProxy.enqueue(<StoryCommand>[
    StoryCommand.withAddMod(AddMod(
        modName: ['inspect_mod'],
        modNameTransitional: 'root',
        intent: Intent(action: 'action', handler: _testAppUrl),
        surfaceRelation: SurfaceRelation()))
  ]);
  await storyPuppetMasterProxy.execute();
}

Future<FakeVmoHolder> _readInspect() async {
  // WARNING: 0) These paths are extremely fragile.
  var globs = [
    // TODO(fxb/38305): Address or remove this old TODO, and reenable test in //sdk/dart/BUILD.gn.
    //   CL should have landed so stories reuse session envs. This breaks hardcoded inspect paths.
    //   Old TODO: remove this one once stories reuse session envs.
    '/hub/r/modular_test_harness_*/*/r/session-*/*/r/*/*/c/flutter_*_runner.cmx/*/c/$_testAppName/*/out/{debug,diagnostics}/root.inspect',
    '/hub/r/modular_test_harness_*/*/c/flutter_*_runner.cmx/*/c/$_testAppName/*/out/{debug,diagnostics}/root.inspect',
    '/hub/r/mth_*/*/r/session-*/*/r/*/*/c/flutter_*_runner.cmx/*/c/$_testAppName/*/out/{debug,diagnostics}/root.inspect',
    '/hub/r/mth_*/*/c/flutter_*_runner.cmx/*/c/$_testAppName/*/out/{debug,diagnostics}/root.inspect',
  ];
  for (final globString in globs) {
    await for (var f in Glob(globString).list()) {
      if (f is dartio.File) {
        // WARNING: 1) This read is not atomic.
        // WARNING: 2) This won't work with VMOs written in C++ and maybe elsewhere.
        // TODO(fxbug.dev/4487): Use direct VMO read when possible.
        var vmoBytes = await f.readAsBytes();
        var vmoData = ByteData(vmoBytes.length);
        for (int i = 0; i < vmoBytes.length; i++) {
          vmoData.setUint8(i, vmoBytes[i]);
        }
        var vmo = FakeVmoHolder.usingData(vmoData);
        return vmo;
      }
    }
  }
  throw Exception('could not find inspect node');
}

void main() {
  final controller = ComponentControllerProxy();
  FlutterDriver driver;

  // The following boilerplate is a one time setup required to make
  // flutter_driver work in Fuchsia.
  //
  // When a module built using Flutter starts up in debug mode, it creates an
  // instance of the Dart VM, and spawns an Isolate (isolated Dart execution
  // context) containing your module.
  setUpAll(() async {
    Logger.globalLevel = LoggingLevel.all;

    await _startTestHarness();
    await _launchModUnderTest();

    // Creates an object you can use to search for your mod on the machine
    driver = await FlutterDriver.connect(
        fuchsiaModuleTarget: _testAppName,
        printCommunication: true,
        logCommunicationToFile: false);
  });

  tearDownAll(() async {
    await driver?.close();
    controller.ctrl.close();

    testHarnessProxy.ctrl.close();
    testHarnessController.ctrl.close();
  });

  Future<FakeVmoHolder> tapAndWait(String buttonName, String nextState) async {
    await driver.tap(find.text(buttonName));
    await driver.waitFor(find.byValueKey(nextState));
    return await _readInspect();
  }

  test('Put the program through its paces', () async {
    // Wait for initial StateBloc value to appear
    await driver.waitFor(find.byValueKey('Program has started'));
    var matcher = VmoMatcher(await _readInspect());

    expect(
        matcher.node()
          ..propertyEquals('interesting', 118)
          ..propertyEquals('double down', 3.23)
          ..propertyEquals('bytes', Uint8List.fromList([1, 2, 3, 4]))
          ..propertyEquals('greeting', 'Hello World'),
        hasNoErrors);

    expect(
        matcher.node().at(['home-page'])
          ..propertyEquals('counter', 0)
          ..propertyEquals('background-color', 'Color(0xffffffff)')
          ..propertyEquals('title', 'Hello Inspect!'),
        hasNoErrors);

    matcher = VmoMatcher(
        await tapAndWait('Increment counter', 'Counter was incremented'));
    expect(matcher.node().at(['home-page'])..propertyEquals('counter', 1),
        hasNoErrors);

    matcher = VmoMatcher(
        await tapAndWait('Decrement counter', 'Counter was decremented'));
    expect(matcher.node().at(['home-page'])..propertyEquals('counter', 0),
        hasNoErrors);

    // The node name below is truncated due to limitations of the maximum node
    // name length.
    matcher = VmoMatcher(await tapAndWait('Make tree', 'Tree was made'));
    expect(
        matcher.node().at([
          'home-page',
          'I think that I shall never see01234567890123456789012345'
        ])
          ..propertyEquals('int0', 0),
        hasNoErrors);

    matcher = VmoMatcher(await tapAndWait('Grow tree', 'Tree was grown'));
    expect(
        matcher.node().at([
          'home-page',
          'I think that I shall never see01234567890123456789012345'
        ])
          ..propertyEquals('int0', 0)
          ..propertyEquals('int1', 1),
        hasNoErrors);

    matcher = VmoMatcher(await tapAndWait('Delete tree', 'Tree was deleted'));
    expect(
        matcher.node().at(['home-page'])
          ..missingChild(
              'I think that I shall never see01234567890123456789012345'),
        hasNoErrors);

    matcher = VmoMatcher(await tapAndWait('Grow tree', 'Tree was grown'));
    expect(
        matcher.node().at(['home-page'])
          ..missingChild(
              'I think that I shall never see01234567890123456789012345'),
        hasNoErrors);

    matcher = VmoMatcher(await tapAndWait('Make tree', 'Tree was made'));
    expect(
        matcher.node().at([
          'home-page',
          'I think that I shall never see01234567890123456789012345'
        ])
          ..propertyEquals('int3', 3),
        hasNoErrors);

    matcher = VmoMatcher(await tapAndWait('Get answer', 'Waiting for answer'));
    expect(
        matcher.node().at(['home-page'])
          ..propertyEquals('waiting', 'for a hint'),
        hasNoErrors);

    matcher = VmoMatcher(await tapAndWait('Give hint', 'Displayed answer'));
    expect(
        matcher.node().at(['home-page'])..missingChild('waiting'), hasNoErrors);

    matcher = VmoMatcher(await tapAndWait('Change color', 'Color was changed'));
    expect(
        matcher.node().at(['home-page']).propertyNotEquals(
            'background-color', 'Color(0xffffffff)'),
        hasNoErrors);
  });
}
