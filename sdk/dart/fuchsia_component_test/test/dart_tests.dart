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
const String v2EchoServerWithBinderUrl = '#meta/echo_server_with_binder.cm';

void checkCommonExceptions(Exception err, StackTrace stacktrace) {
  if (err is fidl.MethodException<fcomponent.Error>) {
    late final String errorName;
    for (final name in fcomponent.Error.$valuesMap.keys) {
      if (err.value == fcomponent.Error.$valuesMap[name]) {
        errorName = name;
        break;
      }
    }
    log.warning('fidl.$err: fuchsia.component.Error.$errorName');
  } else if (err is fidl.MethodException<fctest.RealmBuilderError2>) {
    late final String errorName;
    for (final name in fctest.RealmBuilderError2.$valuesMap.keys) {
      if (err.value == fctest.RealmBuilderError2.$valuesMap[name]) {
        errorName = name;
        break;
      }
    }
    log.warning('fidl.$err: fuchsia.component.test.Error.$errorName');
  } else if (err is fidl.MethodException) {
    log.warning('fidl.MethodException<${err.value.runtimeType}>($err)');
  } else if (err is fidl.FidlError) {
    log.warning('fidl.${err.runtimeType}($err), FidlErrorCode: ${err.code}');
  } else {
    log.warning('caught exception: ${err.runtimeType}($err)');
  }
  log.warning('stacktrace (if available)...\n${stacktrace.toString()}');
}

void main() {
  setupLogger(name: 'fuchsia-component-test-dart-tests');

  group('realm builder tests', () {
    group('RealmBuilder with CFv2 child', () {
      test('RealmBuilder.create()', () async {
        final builder = await RealmBuilder.create();
        expect(
          builder.addChild(
            'v2EchoServer',
            v2EchoServerUrl,
            ChildOptions()..eager(),
          ),
          completes,
        );
      });

      test('RealmBuilder with legacy (CFv1) child', () async {
        final builder = await RealmBuilder.create();
        expect(
          builder.addLegacyChild(
            'v1EchoServer',
            v1EchoServerUrl,
            ChildOptions(),
          ),
          completes,
        );
      });

      test('RealmBuilder from Component decl', () async {
        final builder = await RealmBuilder.create();
        final decl = fdecl.Component();
        expect(
          builder.addChildFromDecl(
            'componentFromDecl',
            decl,
            ChildOptions(),
          ),
          completes,
        );
      });
    });

    group('basic RealmBuilder tests', () {
      test('protocol and directory capabilities', () async {
        late final RealmInstance? realmInstance;
        try {
          final builder = await RealmBuilder.create();

          final v2EchoServer = await builder.addChild(
            'v2EchoServer',
            v2EchoServerUrl,
          );

          await builder.addRoute(Route()
            ..capability(DirectoryCapability('hub')..rights = fio.rStarDir)
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

          final lifecycleController =
              await realmInstance.root.connectToProtocolInDirPath(
            fsys2.LifecycleControllerProxy(),
            'hub/debug',
          );
          await lifecycleController.stop('./v2EchoServer', true);
        } on Exception catch (err, stacktrace) {
          checkCommonExceptions(err, stacktrace);
          rethrow;
        } finally {
          if (realmInstance != null) {
            await realmInstance.root.close();
          }
        }
      });

      test('connectRequestToNamedProtocol', () async {
        late final RealmInstance? realmInstance;
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
        late final RealmInstance? realmInstance;
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
      late final RealmInstance? realmInstance;
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
    //   late final RealmInstance? realmInstance;
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

    test('replace realm decl', () async {
      try {
        final builder = await RealmBuilder.create();
        var origRootDecl = await builder.getRealmDecl();
        expect(origRootDecl, fdecl.Component());
        // TODO(fxbug.dev/96610): FIDL Table bindings for Dart declare fields
        // `final` and the default fdecl.Component sets children to null. Since
        // the above `expect()` guarantees the Component is equal to the default
        // constructor result, we don't have to copy fields from the original,
        // in THIS case. But we need a way to modify Dart FIDL Table bindings.
        final rootDecl = fdecl.Component(
          children: [],
        );
        rootDecl.children!.add(fdecl.Child(
          name: 'example-child',
          url: 'example://url',
          startup: fdecl.StartupMode.eager,
        ));
        await builder.replaceRealmDecl(rootDecl);
        expect(rootDecl, await builder.getRealmDecl());
      } on Exception catch (err, stacktrace) {
        checkCommonExceptions(err, stacktrace);
        rethrow;
      }
    });

    test('replace component decl', () async {
      final eventStreamBinding = fsys2.EventStreamBinding();
      late final RealmInstance? realmInstance;
      try {
        final builder = await RealmBuilder.create();

        const echoServerName = 'v2EchoServer';

        final v2EchoServer = await builder.addChild(
          echoServerName,
          v2EchoServerUrl,
        );

        var decl = await builder.getComponentDecl(v2EchoServer);

        decl.exposes!.add(
          fdecl.Expose.withProtocol(
            fdecl.ExposeProtocol(
              source: fdecl.Ref.withSelf(fdecl.SelfRef()),
              sourceName: fecho.Echo.$serviceName,
              target: fdecl.Ref.withParent(fdecl.ParentRef()),
              targetName: 'renamedEchoService',
            ),
          ),
        );

        await builder.replaceComponentDecl(v2EchoServer, decl);

        // Route logging to child
        await builder.addRoute(Route()
          ..capability(ProtocolCapability(flogger.LogSink.$serviceName))
          ..from(Ref.parent())
          ..to(Ref.child(v2EchoServer)));

        await builder.addRoute(Route()
          ..capability(ProtocolCapability('renamedEchoService'))
          ..from(Ref.child(v2EchoServer))
          ..to(Ref.parent()));

        // Start the realmInstance. The EchoServer is not "eager", so it should
        // not start automatically.
        realmInstance = await builder.build();

        final echo = await realmInstance.root.connectToProtocolAtPath(
          fecho.EchoProxy(),
          'renamedEchoService',
        );
        const testString = 'ECHO...Echo...echo...(echo)...';

        final reply = await echo.echoString(testString);

        expect(testString, reply);
      } on Exception catch (err, stacktrace) {
        checkCommonExceptions(err, stacktrace);
        rethrow;
      } finally {
        if (realmInstance != null) {
          await realmInstance.root.close();
        }
        eventStreamBinding.close();
      }
    });

    test('start by binding', () async {
      final eventStreamBinding = fsys2.EventStreamBinding();
      late final RealmInstance? realmInstance;
      late final String? serverMoniker;
      try {
        final builder = await RealmBuilder.create();

        const serverName = 'v2Server';
        const serverBinder = 'serverBinder';

        // This test leverages the `echo_server` binary, with an augmented
        // component manifest that exposes `fuchsia.component.Binder`. The
        // `echo_server` will automatically start if a client connects to its
        // `Echo` service, but this test doesn't do that. It leverages the fact
        // that the component will launch and execute a loop waiting for
        // requests, which can make the state easier to bug if the event does
        // not arrive, or if a future iteration of the test adds a step to
        // stop the component (via the LifecycleController) and wait for the
        // "stopped" event.
        final v2Server = await builder.addChild(
          serverName,
          v2EchoServerWithBinderUrl,
        );

        // Route logging to child
        await builder.addRoute(Route()
          ..capability(ProtocolCapability(flogger.LogSink.$serviceName))
          ..from(Ref.parent())
          ..to(Ref.child(v2Server)));

        // Route the child's Binder service to parent, so the test can connect
        // to it to start the child.
        await builder.addRoute(Route()
          ..capability(ProtocolCapability(fcomponent.Binder.$serviceName,
              as: serverBinder))
          ..from(Ref.child(v2Server))
          ..to(Ref.parent()));

        // Route the framework's EventSource so the test can await the server's
        // "started" and "stopped" events.
        await builder.addRoute(Route()
          ..capability(ProtocolCapability(fsys2.EventSource.$serviceName))
          ..from(Ref.framework())
          ..to(Ref.parent()));

        // Connect to the framework's EventSource.
        final eventSource = fsys2.EventSourceProxy();
        await (services.Incoming.fromSvcPath()..connectToService(eventSource))
            .close();

        // Register callbacks for started and stopped events, and complete a
        // Future when called.
        final completeWaitForStart = Completer();
        final eventStreamClientEnd = eventStreamBinding.wrap(
          OnEvent(
            started: (String moniker) {
              if (moniker == serverMoniker) {
                log.info('start completed for $moniker');
                completeWaitForStart.complete();
              }
            },
          ),
        );

        // Subscribe to "started" events
        await eventSource.subscribe(
          [
            fsys2.EventSubscription(eventName: 'started'),
          ],
          eventStreamClientEnd,
        );

        // Start the realmInstance. The child component (the server) is not
        // "eager", so it should not start automatically.
        realmInstance = await builder.build();
        final scopedInstance = realmInstance.root;
        serverMoniker = './${scopedInstance.collectionName}:'
            '${scopedInstance.childName}/$serverName';

        // Start the server
        /*serverBinder=*/ await scopedInstance.connectToProtocolAtPath(
          fcomponent.BinderProxy(),
          serverBinder,
        );

        log.info('connected to server Binder; waiting for start event');

        // Wait for the server "started" event
        await completeWaitForStart.future;

        log.info('got start');

        // Note, since this test abruptly stopped the child component, a
        // non-zero (error) status is likely.
      } on Exception catch (err, stacktrace) {
        checkCommonExceptions(err, stacktrace);
        rethrow;
      } finally {
        if (realmInstance != null) {
          await realmInstance.root.close();
        }
        eventStreamBinding.close();
      }
    });

    test('route echo between two v2 components', () async {
      final eventStreamBinding = fsys2.EventStreamBinding();
      late final RealmInstance? realmInstance;
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
          OnEvent(stopped: (String moniker, int status) {
            // Since EchoClient is [eager()], it may start and stop before the
            // async [builder.build()] completes. [realmInstance.root.childName]
            // would not be known before this stopped event is received, so
            // [endsWith()] is the best solution here.
            if (moniker.endsWith('/$echoClientName')) {
              completeWaitForStop.complete(status);
            }
          }),
        );

        // Subscribe for "stopped" events.
        //
        // NOTE: This requires the test CML include a `use` for the subscribed
        // event type(s), for example:
        //
        // ```cml
        //   use: [
        //     { protocol: "fuchsia.sys2.EventSource" },
        //     {
        //         event: [
        //             "started",
        //             "stopped",
        //         ],
        //         from: "framework",
        //     },
        //   ],
        // ```
        await eventSource.subscribe(
          [fsys2.EventSubscription(eventName: 'stopped')],
          eventStreamClientEnd,
        );

        // Start the realm instance.
        realmInstance = await builder.build();

        // Wait for the client to stop, and check for a successful exit status.
        final stoppedStatus = await completeWaitForStop.future;
        expect(stoppedStatus, 0);
      } on Exception catch (err, stacktrace) {
        checkCommonExceptions(err, stacktrace);
        rethrow;
      } finally {
        if (realmInstance != null) {
          await realmInstance.root.close();
        }
        eventStreamBinding.close();
      }
    });
  });
}
