// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fuchsia_component_test/realm_builder.dart';

import 'package:fidl/fidl.dart' as fidl;
import 'package:fidl_fidl_examples_routing_echo/fidl_async.dart' as fecho;
import 'package:fidl_fuchsia_component/fidl_async.dart' as fcomponent;
import 'package:fidl_fuchsia_component_test/fidl_async.dart' as fctest;
import 'package:fidl_fuchsia_component_decl/fidl_async.dart' as fdecl;
import 'package:fidl_fuchsia_io/fidl_async.dart' as fio;
import 'package:fidl_fuchsia_logger/fidl_async.dart' as flogger;
import 'package:fidl_fuchsia_sys2/fidl_async.dart' as fsys2;

import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart' as services;

import 'package:test/test.dart';

const String v1EchoClientUrl =
    'fuchsia-pkg://fuchsia.com/dart_realm_builder_unittests#meta/echo_client.cmx';
const String v2EchoClientUrl = '#meta/echo_client.cm';
const String v1EchoServerUrl =
    'fuchsia-pkg://fuchsia.com/dart_realm_builder_unittests#meta/echo_server.cmx';
const String v2EchoServerUrl = '#meta/echo_server.cm';

void main() {
  setupLogger(name: 'fuchsia-component-test-dart-tests');

  group('realm builder tests', () {
    group('RealmBuilder with CFv2 child', () {
      test('RealmBuilder.create()', () async {
        final builder = await RealmBuilder.create();
        expect(
            builder.addChild(
                'v2EchoServer', v2EchoServerUrl, ChildOptions()..eager()),
            completes);
      });

      test('RealmBuilder with legacy (CFv1) child', () async {
        final builder = await RealmBuilder.create();
        expect(
            builder.addLegacyChild(
                'v1EchoServer', v1EchoServerUrl, ChildOptions()),
            completes);
      });

      test('RealmBuilder from Component decl', () async {
        final builder = await RealmBuilder.create();
        final decl = fdecl.Component();
        expect(
            builder.addChildFromDecl('componentFromDecl', decl, ChildOptions()),
            completes);
      });
    });

    group('basic RealmBuilder tests', () {
      test('protocol and directory capabilities', () async {
        RealmInstance? realmInstance;
        try {
          final builder = await RealmBuilder.create();

          final v2EchoServer = await builder.addChild(
            'v2EchoServer',
            v2EchoServerUrl,
          );

          await builder.addRoute(Route()
            ..capability(DirectoryCapability('hub')..rights = fio.rStarDir)
            ..capability(DirectoryCapability('tmp')..rights = fio.rStarDir)
            ..from(Ref.framework())
            ..to(Ref.parent()));

          await builder.addRoute(Route()
            ..capability(ProtocolCapability(flogger.LogSink.$serviceName))
            ..from(Ref.parent())
            ..to(Ref.child(v2EchoServer)));

          await builder.addRoute(Route()
            ..capability(ProtocolCapability(fecho.Echo.$serviceName))
            ..from(Ref.child(v2EchoServer))
            ..to(Ref.parent()));

          realmInstance = await builder.build();

          final echo = realmInstance.root
              .connectToProtocolAtExposedDir(fecho.EchoProxy());
          const testString = 'ECHO...Echo...echo...(echo)...';

          final reply = await echo.echoString(testString);

          expect(testString, reply);

          final lifecycleController = await realmInstance.root
              .connectToProtocolInDirPath(
                  fsys2.LifecycleControllerProxy(), 'hub/debug');
          await lifecycleController.stop('./v2EchoServer', true);
        } on fidl.MethodException<fcomponent.Error> catch (err, stacktrace) {
          late final String errorName;
          for (final name in fcomponent.Error.$valuesMap.keys) {
            if (err.value == fcomponent.Error.$valuesMap[name]) {
              errorName = name;
              break;
            }
          }
          log.warning(
            'fidl.$err: fuchsia.component.Error.$errorName, '
            'stacktrace if any... \n$stacktrace',
          );
          rethrow;
        } on fidl.FidlError catch (err, stacktrace) {
          log.warning(
              'fidl.${err.runtimeType}($err), FidlErrorCode: ${err.code}, '
              'stacktrace: ${stacktrace.toString()}');
          rethrow;
        } on Exception catch (err, stacktrace) {
          log.warning('caught exception: ${err.runtimeType}($err), '
              'stacktrace: ${stacktrace.toString()}');
          rethrow;
        } finally {
          if (realmInstance != null) {
            await realmInstance.root.close();
          }
        }
      });

      test('connectRequestToNamedProtocol', () async {
        RealmInstance? realmInstance;
        try {
          final builder = await RealmBuilder.create();

          final v2EchoServer = await builder.addChild(
            'v2EchoServer',
            v2EchoServerUrl,
          );

          await builder.addRoute(Route()
            ..capability(ProtocolCapability(flogger.LogSink.$serviceName))
            ..from(Ref.parent())
            ..to(Ref.child(v2EchoServer)));

          await builder.addRoute(Route()
            ..capability(ProtocolCapability(fecho.Echo.$serviceName))
            ..from(Ref.child(v2EchoServer))
            ..to(Ref.parent()));

          realmInstance = await builder.build();

          final echo = fecho.EchoProxy();
          realmInstance.root.connectRequestToNamedProtocolAtExposedDir(
            fecho.Echo.$serviceName,
            echo.ctrl.request().passChannel()!,
          );

          const testString = 'ECHO...Echo...echo...(echo)...';
          final reply = await echo.echoString(testString);
          expect(testString, reply);
        } finally {
          if (realmInstance != null) {
            await realmInstance.root.close();
          }
        }
      });

      test('connectToProtocol using legacy component', () async {
        RealmInstance? realmInstance;
        try {
          final builder = await RealmBuilder.create();

          final v1EchoServer = await builder.addLegacyChild(
            'v1EchoServer',
            v1EchoServerUrl,
          );

          await builder.addRoute(Route()
            ..capability(ProtocolCapability(flogger.LogSink.$serviceName))
            ..from(Ref.parent())
            ..to(Ref.child(v1EchoServer)));

          await builder.addRoute(Route()
            ..capability(ProtocolCapability(fecho.Echo.$serviceName))
            ..from(Ref.child(v1EchoServer))
            ..to(Ref.parent()));

          realmInstance = await builder.build();

          final echo = realmInstance.root
              .connectToProtocolAtExposedDir(fecho.EchoProxy());

          const testString = 'ECHO...Echo...echo...(echo)...';
          final reply = await echo.echoString(testString);
          expect(testString, reply);
        } finally {
          if (realmInstance != null) {
            await realmInstance.root.close();
          }
        }
      });
    });

    test('connect to child in subrealm', () async {
      RealmInstance? realmInstance;
      try {
        const subRealmName = 'sub_realm';

        final realmBuilder = await RealmBuilder.create();
        final subRealmBuilder = await realmBuilder.addChildRealm(subRealmName);

        final v2EchoServer = await subRealmBuilder.addChild(
          'v2EchoServer',
          v2EchoServerUrl,
        );

        // Route LogSink from RealmBuilder to the subRealm.
        await realmBuilder.addRoute(Route()
          ..capability(ProtocolCapability(flogger.LogSink.$serviceName))
          ..from(Ref.parent())
          ..to(Ref.childFromSubRealm(subRealmBuilder)));

        // Route LogSink from the subRealm to the Echo component.
        await subRealmBuilder.addRoute(Route()
          ..capability(ProtocolCapability(flogger.LogSink.$serviceName))
          ..from(Ref.parent())
          ..to(Ref.child(v2EchoServer)));

        // Route the Echo service from the Echo child component to its parent
        // (the subRealm).
        await subRealmBuilder.addRoute(Route()
          ..capability(ProtocolCapability(fecho.Echo.$serviceName))
          ..from(Ref.child(v2EchoServer))
          ..to(Ref.parent()));

        // Route the Echo service from the subRealm child to its parent
        // (the RealmBuilder).
        await realmBuilder.addRoute(Route()
          ..capability(ProtocolCapability(fecho.Echo.$serviceName))
          ..from(Ref.childFromSubRealm(subRealmBuilder))
          ..to(Ref.parent()));

        realmInstance = await realmBuilder.build();

        final echo = fecho.EchoProxy();
        realmInstance.root.connectRequestToNamedProtocolAtExposedDir(
          fecho.Echo.$serviceName,
          echo.ctrl.request().passChannel()!,
        );

        const testString = 'ECHO...Echo...echo...(echo)...';
        final reply = await echo.echoString(testString);
        expect(testString, reply);
      } finally {
        if (realmInstance != null) {
          await realmInstance.root.close();
        }
      }
    });

    // TODO(richkadel): Testing storage from the dart test is not feasible
    // until Dart RealmBuilder supports local child implementations.
    // The code to open the Storage capability seems viable, once there is a
    // way to route the StorageCapability to a Dart component of this test.
    // test('storage', () async {
    //   RealmInstance? realmInstance;
    //   try {
    //     final builder = await RealmBuilder.create();

    //     await builder.addRoute(Route()
    //       ..capability(StorageCapability('data'))
    //       ..from(Ref.parent())
    //       ..to(localChild));

    //     realmInstance = await builder.build();

    // TODO(richkadel): Move the rest of this logic into the local child
    // implementation...

    //     var dataDir = fio.DirectoryProxy();
    //     await realmInstance.root.exposedDir.open(
    //         fio.openRightReadable | fio.openRightWritable,
    //         fio.modeTypeDirectory,
    //         'data',
    //         fidl.InterfaceRequest<fio.Node>(
    //             dataDir.ctrl.request().passChannel()));

    //     log.info('dataDir: ${await dataDir.describe()}');

    //     final exampleFile = fio.File2Proxy();
    //     await dataDir.open(
    //         fio.openRightReadable |
    //             fio.openRightWritable |
    //             fio.openFlagCreate,
    //         fio.modeTypeFile,
    //         'example_file',
    //         fidl.InterfaceRequest<fio.Node>(
    //             exampleFile.ctrl.request().passChannel()));

    //     final fileDescription = await exampleFile.describe2(
    //         fio.ConnectionInfoQuery.representation |
    //             fio.ConnectionInfoQuery.rights |
    //             fio.ConnectionInfoQuery.availableOperations);
    //     log.info('exampleFile: $fileDescription');

    //     const exampleData = 'example data';

    //    // The following imports are required for string to utf8 conversion.
    //    //   import 'dart:convert' show utf8;
    //    //   import 'dart:typed_data';

    //     final encodedData = utf8.encode(exampleData);
    //     await exampleFile.write(utf8.encode(exampleData) as Uint8List);
    //     await exampleFile.seek(fio.SeekOrigin.start, 0);
    //     final fileContents =
    //         utf8.decode(await exampleFile.read(encodedData.length));
    //     expect(exampleData, fileContents);
    //   } finally {
    //     if (realmInstance != null) {
    //       await realmInstance.root.close();
    //     }
    //   }
    // });

    group('API checks', () {
      test('default ChildOptions', () {
        final foptions = ChildOptions().toFidlType();
        expect(foptions.startup, fdecl.StartupMode.lazy);
        expect(foptions.environment, isNull);
        expect(foptions.onTerminate, fdecl.OnTerminate.none);
      });

      test('set ChildOptions', () {
        const envName = 'someEnv';
        final childOptions = ChildOptions()
          ..eager()
          ..rebootOnTerminate()
          ..environment = envName;
        final foptions = childOptions.toFidlType();
        expect(foptions.startup, fdecl.StartupMode.eager);
        expect(foptions.environment, envName);
        expect(foptions.onTerminate, fdecl.OnTerminate.reboot);
      });

      test('named ScopedInstance', () async {
        const collectionName = 'someCollection';
        final fac = ScopedInstanceFactory(collectionName);
        expect(fac.collectionName, collectionName);
      });

      test('ScopedInstance collectionName not found', () async {
        final fac = ScopedInstanceFactory('badCollectionName');
        ScopedInstance? scopedInstance;

        try {
          scopedInstance = await fac.newNamedInstance('someChild', 'badUrl');
        } on fidl.MethodException<fcomponent.Error> catch (err) {
          expect(err.value, fcomponent.Error.invalidArguments);
        } finally {
          expect(scopedInstance, isNull);
        }

        var caught = false;
        try {
          scopedInstance =
              await fac.newNamedInstance('someChild', '#meta/someComponent.cm');
        } on fidl.MethodException<fcomponent.Error> catch (err) {
          expect(err.value, fcomponent.Error.collectionNotFound);
          caught = true;
        } finally {
          expect(caught, true);
        }

        expect(scopedInstance, isNull);
      });
    });

    group('ScopedInstance checks', () {
      test('defaults', () async {
        final builder = await RealmBuilder.create();
        final realmInstance = await builder.build();
        final scopedInstance = realmInstance.root;
        expect(scopedInstance.collectionName, defaultCollectionName);
        expect(scopedInstance.childName, startsWith('auto-'));
        final nodeInfo = await scopedInstance.exposedDir.describe();
        expect(nodeInfo.$tag, fio.NodeInfoTag.directory);
        expect(scopedInstance.close(), completes);
      });
    });

    group('Start test component', () {
      test('start by binding', () async {
        final builder = await RealmBuilder.create();

        /*childRef=*/ await builder.addChild('v2EchoServer', v2EchoServerUrl);

        await builder.addRoute(Route()
          ..capability(DirectoryCapability('hub')..rights = fio.rStarDir)
          ..from(Ref.framework())
          ..to(Ref.parent()));

        final realmInstance = await builder.build();
        final scopedInstance = realmInstance.root;

        final lifecycleController =
            await scopedInstance.connectToProtocolInDirPath(
                fsys2.LifecycleControllerProxy(), 'hub/debug');

        var caught = false;
        try {
          await lifecycleController.stop(
              './${scopedInstance.collectionName}:${scopedInstance.childName}',
              true);
        } on fidl.MethodException<fcomponent.Error> catch (err) {
          expect(err.value, fcomponent.Error.instanceNotFound);
          caught = true;
        } finally {
          expect(caught, true);
        }

        /*binderProxy=*/ scopedInstance.connectToBinder();

        // Note that since Fuchsia doesn't yet have EventMatcher API bindings
        // for Dart, there is no easy way to block and wait until the component
        // starts (without using it), so the test can't reliably call stop here.

        expect(scopedInstance.close(), completes);
      });
    });

    test('route echo between two v2 components', () async {
      final eventStreamBinding = fsys2.EventStreamBinding();
      RealmInstance? realmInstance;
      try {
        final builder = await RealmBuilder.create();

        const echoServerName = 'v2EchoServer';
        const echoClientName = 'v2EchoClient';

        final v2EchoServer = await builder.addChild(
          echoServerName,
          v2EchoServerUrl,
        );
        final v2EchoClient = await builder.addChild(
          echoClientName,
          v2EchoClientUrl,
          ChildOptions()..eager(),
        );

        // Route logging to children
        await builder.addRoute(Route()
          ..capability(ProtocolCapability(flogger.LogSink.$serviceName))
          ..from(Ref.parent())
          ..to(Ref.child(v2EchoServer))
          ..to(Ref.child(v2EchoClient)));

        // Route the echo service from server to client
        await builder.addRoute(Route()
          ..capability(ProtocolCapability(fecho.Echo.$serviceName))
          ..from(Ref.child(v2EchoServer))
          ..to(Ref.child(v2EchoClient)));

        // Route the framework's EventSource so the test can await the echo
        // client's termination and verify a successful exit status.
        await builder.addRoute(Route()
          ..capability(ProtocolCapability(fsys2.EventSource.$serviceName))
          ..from(Ref.framework())
          ..to(Ref.parent()));

        // Connect to the framework's EventSource.
        final eventSource = fsys2.EventSourceProxy();
        await (services.Incoming.fromSvcPath()..connectToService(eventSource))
            .close();

        // Register a callback for stopped events, and complete a Future when
        // the event client stops.
        final completeWaitForStop = Completer<int>();
        final eventStreamClientEnd = eventStreamBinding.wrap(
          OnEvent.stopped((String moniker, int status) {
            if (moniker.endsWith('/$echoClientName')) {
              completeWaitForStop.complete(status);
            }
          }),
        );

        // Subscribe for `stopped` events.
        await eventSource.subscribe(
          [fsys2.EventSubscription(eventName: 'stopped')],
          eventStreamClientEnd,
        );

        // Start the realm instance.
        realmInstance = await builder.build();

        // Wait for the client to stop, and check for a successful exit status.
        final stoppedStatus = await completeWaitForStop.future;
        expect(stoppedStatus, 0);
      } on fidl.MethodException<fcomponent.Error> catch (err, stacktrace) {
        late final String errorName;
        for (final name in fcomponent.Error.$valuesMap.keys) {
          if (err.value == fcomponent.Error.$valuesMap[name]) {
            errorName = name;
            break;
          }
        }
        log.warning(
          'fidl.$err: fuchsia.component.Error.$errorName, '
          'stacktrace if any... \n$stacktrace',
        );
        rethrow;
      } on fidl
          .MethodException<fctest.RealmBuilderError2> catch (err, stacktrace) {
        late final String errorName;
        for (final name in fctest.RealmBuilderError2.$valuesMap.keys) {
          if (err.value == fctest.RealmBuilderError2.$valuesMap[name]) {
            errorName = name;
            break;
          }
        }
        log.warning(
          'fidl.$err: fuchsia.component.Error.$errorName, '
          'stacktrace if any... \n$stacktrace',
        );
        rethrow;
      } on fidl.FidlError catch (err, stacktrace) {
        log.warning(
            'fidl.${err.runtimeType}($err), FidlErrorCode: ${err.code}, '
            'stacktrace: ${stacktrace.toString()}');
        rethrow;
      } on Exception catch (err, stacktrace) {
        log.warning('caught exception: ${err.runtimeType}($err), '
            'stacktrace: ${stacktrace.toString()}');
        rethrow;
      } finally {
        log.info('finally!');
        if (realmInstance != null) {
          await realmInstance.root.close();
          log.info('realmInstance.root closed');
        }
        eventStreamBinding.close();
      }
    });
  });
}
