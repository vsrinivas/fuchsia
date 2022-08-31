// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: prefer_const_declarations, parameter_assignments, non_constant_identifier_names, join_return_with_assignment

import 'dart:io';

import 'package:args/args.dart';
import 'package:fidl_fuchsia_ui_app/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:fuchsia_services/services.dart';
import 'package:zircon/zircon.dart';

final _argsCsvFilePath = '/config/data/args.csv';

Future<void> main(List<String> args) async {
  args = args + _GetArgsFromConfigFile();
  WidgetsFlutterBinding.ensureInitialized();
  final parser = ArgParser()
    ..addFlag('showOverlay', defaultsTo: false)
    ..addFlag('hitTestable', defaultsTo: true)
    ..addFlag('focusable', defaultsTo: true)
    ..addFlag('useFlatland', defaultsTo: false)
    ..addFlag('usePointerInjection2', defaultsTo: false);
  final arguments = parser.parse(args);
  for (final option in arguments.options) {
    print('parent-view: $option: ${arguments[option]}');
  }

  final useFlatland = arguments['useFlatland'];
  if (useFlatland) {
    final viewportCreationToken = _launchFlatlandApp();
    runApp(MaterialApp(
      debugShowCheckedModeBanner: false,
      home: TestApp(
        FuchsiaViewConnection.flatland(viewportCreationToken,
            usePointerInjection2: arguments['usePointerInjection2']),
        showOverlay: arguments['showOverlay'],
        hitTestable: arguments['hitTestable'],
        focusable: arguments['focusable'],
      ),
    ));
  } else {
    final childViewArgs = _launchGfxApp();
    runApp(MaterialApp(
      debugShowCheckedModeBanner: false,
      home: TestApp(
        FuchsiaViewConnection(childViewArgs[0] /*ViewHolderToken*/,
            viewRef: childViewArgs[1] /*ViewRef*/,
            usePointerInjection2: arguments['usePointerInjection2']),
        showOverlay: arguments['showOverlay'],
        hitTestable: arguments['hitTestable'],
        focusable: arguments['focusable'],
        usePointerInjection2: arguments['usePointerInjection2'],
      ),
    ));
  }
}

class TestApp extends StatelessWidget {
  static const _black = Color.fromARGB(255, 0, 0, 0);
  static const _blue = Color.fromARGB(255, 0, 0, 255);

  final FuchsiaViewConnection connection;
  final bool showOverlay;
  final bool hitTestable;
  final bool focusable;
  final bool usePointerInjection2;

  final _backgroundColor = ValueNotifier(_blue);

  TestApp(this.connection,
      {this.showOverlay = false,
      this.hitTestable = true,
      this.focusable = true,
      this.usePointerInjection2 = false});

  @override
  Widget build(BuildContext context) {
    return Listener(
      // When testing pointer injection, a gesture recognizer here would steal
      // input from the child platform view's gesture recognizer.  In that case,
      // don't use any gesture recognizer here.
      onPointerUp:
          usePointerInjection2 ? null : (_) => _backgroundColor.value = _black,
      child: AnimatedBuilder(
          animation: _backgroundColor,
          builder: (context, snapshot) {
            return Container(
              color: _backgroundColor.value,
              child: Stack(
                alignment: Alignment.center,
                children: [
                  FractionallySizedBox(
                    widthFactor: 0.33,
                    heightFactor: 0.33,
                    child: FuchsiaView(
                      controller: connection,
                      hitTestable: hitTestable,
                      focusable: focusable,
                    ),
                  ),
                  if (showOverlay)
                    FractionallySizedBox(
                      widthFactor: 0.66,
                      heightFactor: 0.66,
                      child: Container(
                        alignment: Alignment.topRight,
                        child: FractionallySizedBox(
                          widthFactor: 0.5,
                          heightFactor: 0.5,
                          child: Container(
                            color: Color.fromARGB(255, 0, 255, 0),
                          ),
                        ),
                      ),
                    ),
                ],
              ),
            );
          }),
    );
  }
}

ViewportCreationToken _launchFlatlandApp() {
  ViewProviderProxy viewProvider = ViewProviderProxy();
  Incoming.fromSvcPath()
    ..connectToService(viewProvider)
    ..close();

  final viewTokens = ChannelPair();
  assert(viewTokens.status == ZX.OK);
  final viewportCreationToken = ViewportCreationToken(value: viewTokens.first);
  final viewCreationToken = ViewCreationToken(value: viewTokens.second);

  final createViewArgs = CreateView2Args(viewCreationToken: viewCreationToken);
  viewProvider.createView2(createViewArgs);
  viewProvider.ctrl.close();

  return viewportCreationToken;
}

List _launchGfxApp() {
  ViewProviderProxy viewProvider = ViewProviderProxy();
  Incoming.fromSvcPath()
    ..connectToService(viewProvider)
    ..close();

  final viewTokens = EventPairPair();
  final viewRefTokens = EventPairPair();

  assert(viewTokens.status == ZX.OK);
  assert(viewRefTokens.status == ZX.OK);

  final viewHolderToken = ViewHolderToken(value: viewTokens.first);
  final viewToken = ViewToken(value: viewTokens.second);

  final viewRefClone =
      ViewRef(reference: viewRefTokens.first.duplicate((ZX.RIGHTS_BASIC)));
  final viewRef = ViewRef(reference: viewRefTokens.first);
  final viewRefControl = ViewRefControl(
      reference: viewRefTokens.second
          .duplicate(ZX.DEFAULT_EVENTPAIR_RIGHTS & (~ZX.RIGHT_DUPLICATE)));

  viewProvider.createViewWithViewRef(
      viewToken.value, viewRefControl, viewRefClone);
  viewProvider.ctrl.close();

  return [viewHolderToken, viewRef];
}

List<String> _GetArgsFromConfigFile() {
  List<String> args;
  final f = File(_argsCsvFilePath);
  if (!f.existsSync()) {
    return List.empty();
  }
  final fileContentCsv = f.readAsStringSync();
  args = fileContentCsv.split('\n');
  return args;
}
