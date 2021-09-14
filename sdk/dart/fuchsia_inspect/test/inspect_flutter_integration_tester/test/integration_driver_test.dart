// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'dart:io' as dartio;

import 'package:fidl_fuchsia_modular/fidl_async.dart';
import 'package:fidl_fuchsia_modular_testing/fidl_async.dart';
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:fuchsia_remote_debug_protocol/logging.dart';
import 'package:fuchsia_services/services.dart';
import 'package:glob/glob.dart';
import 'package:glob/list_local_fs.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart' show Channel;

// With the current test harness, the program's directory name has to be
// the same as the program's name.
const String _testProgramName = 'inspect_flutter_integration_tester';
const Pattern _testAppName = '$_testProgramName.cmx';
const _testAppUrl =
    'fuchsia-pkg://fuchsia.com/$_testProgramName#meta/$_testAppName';
const _modularTestHarnessURL =
    'fuchsia-pkg://fuchsia.com/modular_test_harness#meta/modular_test_harness.cmx';

TestHarnessProxy testHarnessProxy = TestHarnessProxy();
ComponentControllerProxy testHarnessController = ComponentControllerProxy();

// Starts Modular TestHarness with dev shells. This should be called from within
// a try/finally or similar construct that closes the component controller.
// TODO(fxbug.dev/4487): Use launchTestHarness() from test_harness_fixtures.dart.
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

  /// Converts a Service to URL map into list of [ComponentService].
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
        'fuchsia.vulkan.loader.Loader',
      ],
      envServices: EnvironmentServicesSpec(
          servicesFromComponents: _toComponentServices({
            'fuchsia.identity.account.AccountManager':
                'fuchsia-pkg://fuchsia.com/account_manager#meta/account_manager.cmx',
            'fuchsia.devicesettings.DeviceSettingsManager':
                'fuchsia-pkg://fuchsia.com/device_settings_manager#meta/device_settings_manager.cmx',
            'fuchsia.fonts.Provider':
                'fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx',
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
          }),
          serviceDir: Channel.fromFile('/svc')));
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
        modName: ['my_integration_test_mod'],
        modNameTransitional: 'root',
        intent: Intent(action: 'action', handler: _testAppUrl),
        surfaceRelation: SurfaceRelation()))
  ]);
  await storyPuppetMasterProxy.execute();
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

  Future<int> inspectVmoSize() async {
    // TODO(fxbug.dev/4487): Make this less brittle.
    // TODO(fxb/38305): Address and/or update above TODO, and reenable test in //topaz/BUILD.gn.
    //   CL should have landed so stories reuse session envs. This breaks hardcoded inspect paths.
    var globs = [
      '/hub/r/modular_test_harness_*/*/r/session-*/*/r/*/*/c/flutter*/*/c/$_testAppName/*/out/diagnostics/*',
      '/hub/r/mth_*/*/r/session-*/*/r/*/*/c/flutter*/*/c/$_testAppName/*/out/diagnostics/*'
    ];
    for (final globString in globs) {
      var fileEntity = await Glob(globString).list().toList();
      if (fileEntity.isNotEmpty) {
        var f = dartio.File(fileEntity[0].path);
        var vmoBytes = await f.readAsBytes();
        return vmoBytes.length;
      }
    }
    throw Exception('could not find inspect node');
  }

  Future<void> tapAndWait(String buttonName, String nextState) async {
    await driver.tap(find.text(buttonName));
    await driver.waitFor(find.byValueKey(nextState));
  }

  test('Put the program through its paces', () async {
    await tapAndWait('Config 4k', 'VMO set to 4k');
    await tapAndWait('Config 16k', 'VMO set to 16k');
    await tapAndWait('Test Inspect Instance', 'Inspect is correct');
    expect(await inspectVmoSize(), 16384);
    await tapAndWait('Config 16k', 'ERROR setting 16k');
  });
}
