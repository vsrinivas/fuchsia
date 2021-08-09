// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';
import 'dart:async';

import 'package:fidl/fidl.dart' as fidl;
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:fidl_fuchsia_ui_app/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:fuchsia_services/services.dart';
import 'package:zircon/zircon.dart';
import 'package:fidl_test_touch/fidl_async.dart' as test_touch;

Future<void> main(List<String> args) async {
  WidgetsFlutterBinding.ensureInitialized();

  runApp(MaterialApp(debugShowCheckedModeBanner: false, home: TestApp()));
}

class _TestAppLauncherImpl extends test_touch.TestAppLauncher {
  final Function _onLaunch;

  _TestAppLauncherImpl(this._onLaunch);

  @override
  Future<void> launch(String componentUrl) {
    return _onLaunch(componentUrl);
  }
}

// Creates an app that changes color and calls test.touch.ResponseListenter.Respond when tapped.
// Launches a child app that takes up the left half of the display when it receives a
// test.touch.TestAppLauncher::Launch message.
class TestApp extends StatefulWidget {
  @override
  _TestAppState createState() => _TestAppState();
}

class _TestAppState extends State<TestApp> {
  static const _red = Color.fromARGB(255, 255, 0, 0);
  static const _blue = Color.fromARGB(255, 0, 0, 255);

  final _context = ComponentContext.create();
  final _binding = test_touch.TestAppLauncherBinding();
  _TestAppLauncherImpl _testAppLauncher;

  final _connection = ValueNotifier<FuchsiaViewConnection>(null);
  final _responseListener = test_touch.ResponseListenerProxy();
  final _backgroundColor = ValueNotifier(_blue);

  _TestAppState() {
    _testAppLauncher = _TestAppLauncherImpl((String componentUrl) {
      final completer = Completer();
      _connection.value = FuchsiaViewConnection(_launchApp(componentUrl),
          onViewStateChanged: (_, state) {
        if (state) {
          // Notify test that the child view is ready.
          print('Child view ready');
          completer.complete();
        }
      });
      return completer.future;
    });

    _context.outgoing
      ..addPublicService<test_touch.TestAppLauncher>(
          (fidl.InterfaceRequest<test_touch.TestAppLauncher> serverEnd) =>
              _binding.bind(_testAppLauncher, serverEnd),
          test_touch.TestAppLauncher.$serviceName)
      ..serveFromStartupInfo();

    Incoming.fromSvcPath()
      ..connectToService(_responseListener)
      ..close();

    // We inspect the lower-level data packets, instead of using the higher-level gesture library.
    WidgetsBinding.instance.window.onPointerDataPacket =
        (PointerDataPacket packet) {
      // Record the time when the pointer event was received.
      int nowNanos = System.clockGetMonotonic();

      for (PointerData data in packet.data) {
        print('Flutter received a pointer: ${data.toStringFull()}');
        if (data.change == PointerChange.down) {
          if (_backgroundColor.value == _blue) {
            _backgroundColor.value = _red;
          } else {
            _backgroundColor.value = _blue;
          }

          _responseListener.respond(test_touch.PointerData(
              // Notify test that input was seen.
              localX: data.physicalX,
              localY: data.physicalY,
              timeReceived: nowNanos,
              componentName: 'embedding-flutter'));
        }
      }
    };
  }

  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder(
        valueListenable: _backgroundColor,
        builder: (context, _, __) {
          return ValueListenableBuilder(
              valueListenable: _connection,
              builder: (context, ___, _____) {
                return Container(
                  color: _backgroundColor.value,
                  child: Stack(
                    alignment: Alignment.centerLeft,
                    children: [
                      if (_connection.value != null)
                        FractionallySizedBox(
                          widthFactor: 0.5,
                          heightFactor: 1,
                          child: FuchsiaView(
                            controller: _connection.value,
                            hitTestable: true,
                            focusable: true,
                          ),
                        ),
                    ],
                  ),
                );
              });
        });
  }
}

ViewHolderToken _launchApp(String componentUrl) {
  print('Launching child : $componentUrl');
  final incomingFromChild = Incoming();
  final componentController = ComponentControllerProxy();

  final launcher = LauncherProxy();
  Incoming.fromSvcPath()
    ..connectToService(launcher)
    ..close();
  launcher.createComponent(
    LaunchInfo(
      url: componentUrl,
      directoryRequest: incomingFromChild.request().passChannel(),
    ),
    componentController.ctrl.request(),
  );
  launcher.ctrl.close();

  ViewProviderProxy viewProvider = ViewProviderProxy();
  incomingFromChild
    ..connectToService(viewProvider)
    ..close();

  final viewTokens = EventPairPair();
  assert(viewTokens.status == ZX.OK);
  final viewHolderToken = ViewHolderToken(value: viewTokens.first);
  final viewToken = ViewToken(value: viewTokens.second);

  viewProvider.createView(viewToken.value, null, null);
  viewProvider.ctrl.close();

  return viewHolderToken;
}
