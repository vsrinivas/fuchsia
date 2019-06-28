// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert' show json;
import 'dart:io';

import 'package:fidl_fuchsia_app_discover/fidl_async.dart'
    show Suggestions, SuggestionsBinding, SuggestionsProxy;
import 'package:fidl_fuchsia_shell_ermine/fidl_async.dart' show AskBarProxy;
import 'package:fidl_fuchsia_sys/fidl_async.dart'
    show
        ComponentControllerProxy,
        LauncherProxy,
        LaunchInfo,
        ServiceList,
        ServiceProviderBinding;
import 'package:fidl_fuchsia_ui_app/fidl_async.dart' show ViewProviderProxy;
import 'package:fidl_fuchsia_ui_input/fidl_async.dart' as input;
import 'package:fidl_fuchsia_ui_views/fidl_async.dart'
    show ViewToken, ViewHolderToken;
import 'package:flutter/material.dart';
import 'package:fuchsia_modular_flutter/session_shell.dart' show SessionShell;
import 'package:fuchsia_modular_flutter/story_shell.dart' show StoryShell;
import 'package:fuchsia_scenic_flutter/child_view_connection.dart'
    show ChildViewConnection;
import 'package:fuchsia_services/services.dart' show Incoming, StartupContext;
import 'package:lib.widgets/utils.dart' show PointerEventsListener;
import 'package:zircon/zircon.dart';

import '../utils/elevations.dart';
import '../utils/key_chord_listener.dart'
    show KeyChordListener, KeyChordBinding;
import 'cluster_model.dart';
import 'ermine_service_provider.dart' show ErmineServiceProvider;
import 'status_model.dart';

const _kErmineAskModuleUrl =
    'fuchsia-pkg://fuchsia.com/ermine_ask_module#meta/ermine_ask_module.cmx';

/// Model that manages all the application state of this session shell.
class AppModel {
  final _pointerEventsListener = PointerEventsListener();
  final _componentControllerProxy = ComponentControllerProxy();
  final _suggestionsService = SuggestionsProxy();
  final _ask = AskBarProxy();
  final _cancelActionBinding =
      KeyChordBinding(action: 'cancel', hidUsage: 0x29);

  SessionShell sessionShell;

  final String backgroundImageUrl = 'assets/images/fuchsia.png';
  final Color backgroundColor = Colors.grey[850];
  final _startupContext = StartupContext.fromStartupInfo();

  final ClustersModel clustersModel = ClustersModel();
  final ValueNotifier<DateTime> currentTime =
      ValueNotifier<DateTime>(DateTime.now());
  ValueNotifier<bool> askVisibility = ValueNotifier(false);
  ValueNotifier<ChildViewConnection> askChildViewConnection =
      ValueNotifier<ChildViewConnection>(null);
  ValueNotifier<bool> statusVisibility = ValueNotifier(false);
  ValueNotifier<bool> helpVisibility = ValueNotifier(false);
  KeyChordListener _keyboardListener;
  StatusModel status;
  String keyboardShortcuts = 'Help Me!';

  AppModel() {
    StartupContext.fromStartupInfo()
        .incoming
        .connectToService(_suggestionsService);

    sessionShell = SessionShell(
      startupContext: _startupContext,
      onStoryStarted: clustersModel.addStory,
      onStoryDeleted: clustersModel.removeStory,
    )..start();

    clustersModel.useInProcessStoryShell = _useInProcessStoryShell();
    if (clustersModel.useInProcessStoryShell) {
      StoryShell.advertise(
        startupContext: _startupContext,
        onStoryAttached: clustersModel.getStory,
      );
    }

    status = StatusModel.fromStartupContext(_startupContext);

    // Load the ask bar.
    _loadAskBar();
  }

  /// Called after runApp which initializes flutter's gesture system.
  Future<void> onStarted() async {
    // Capture pointer events directly from Scenic.
    _pointerEventsListener.listen(sessionShell.presentation);

    // Capture key pressess for key bindings in keyboard_shortcuts.json.
    File file = File('/pkg/data/keyboard_shortcuts.json');
    if (file.existsSync()) {
      final bindings = await file.readAsString();
      _keyboardListener = KeyChordListener(
        presentation: sessionShell.presentation,
        actions: {
          'shortcuts': onKeyboard,
          'ask': onMeta,
          'fullscreen': onFullscreen,
          'cancel': onCancel,
          'close': onClose,
          'status': onStatus,
          'nextCluster': clustersModel.nextCluster,
          'previousCluster': clustersModel.previousCluster,
          'logout': onLogout,
        },
        bindings: bindings,
      )..listen();
      keyboardShortcuts = _keyboardListener.helpText();
    } else {
      throw ArgumentError.value(
          'keyboard_shortcuts.json', 'fileName', 'File does not exist');
    }

    // Update the current time every second.
    Timer.periodic(
        Duration(seconds: 1), (timer) => currentTime.value = DateTime.now());

    // Display the Ask bar after a brief duration.
    Timer(Duration(milliseconds: 500), onMeta);

    // Hide the ask bar when:
    // - a story is started from outside of ask bar.
    // - a story toggles fullscreen state.
    // - story cluster changes.
    Listenable.merge([
      clustersModel,
      clustersModel.currentCluster,
      clustersModel.fullscreenStoryNotifier,
    ]).addListener(onCancel);
  }

  void _loadAskBar() {
    final incoming = Incoming();
    final launcherProxy = LauncherProxy();
    _startupContext.incoming.connectToService(launcherProxy);

    launcherProxy.createComponent(
      LaunchInfo(
        url: _kErmineAskModuleUrl,
        directoryRequest: incoming.request().passChannel(),
        additionalServices: ServiceList(
          names: <String>[Suggestions.$serviceName],
          provider: ServiceProviderBinding().wrap(
            ErmineServiceProvider()
              ..advertise<Suggestions>(
                name: Suggestions.$serviceName,
                service: _suggestionsService,
                binding: SuggestionsBinding(),
              ),
          ),
        ),
      ),
      _componentControllerProxy.ctrl.request(),
    );

    final viewProvider = ViewProviderProxy();
    incoming
      ..connectToService(viewProvider)
      ..connectToService(_ask)
      ..close();

    // Create a token pair for the newly-created View.
    final tokenPair = EventPairPair();
    assert(tokenPair.status == ZX.OK);
    final viewHolderToken = ViewHolderToken(value: tokenPair.first);
    final viewToken = ViewToken(value: tokenPair.second);

    viewProvider.createView(viewToken.value, null, null);
    viewProvider.ctrl.close();

    // Load the Ask mod at elevation.
    _ask
      ..onHidden.forEach((_) => askVisibility.value = false)
      ..onVisible.forEach((_) => askVisibility.value = true)
      ..load(elevations.systemOverlayElevation);

    askChildViewConnection.value = ChildViewConnection(viewHolderToken);
  }

  void onFullscreen() {
    if (clustersModel.fullscreenStory != null) {
      clustersModel.fullscreenStory.restore();
    } else if (sessionShell.focusedStory != null) {
      clustersModel.maximize(sessionShell.focusedStory.id);
      // Hide system overlays.
      onCancel();
    }
  }

  /// Shows the Ask bar and sets the focus on it.
  void onMeta() {
    if (askVisibility.value == false) {
      // Close other system overlays.
      onCancel();
      _ask.show();
      _keyboardListener.add(_cancelActionBinding);
    }
  }

  /// Toggles the Status menu on/off.
  void onStatus() {
    if (statusVisibility.value == false) {
      // Close other system overlays.
      onCancel();
      statusVisibility.value = true;
      _keyboardListener.add(_cancelActionBinding);
    }
  }

  /// Called when tapped behind Ask bar, quick settings, notifications or the
  /// Escape key was pressed.
  void onCancel() {
    _ask.hide();
    statusVisibility.value = false;
    helpVisibility.value = false;
    _keyboardListener.release(_cancelActionBinding);
  }

  /// Called when the user wants to delete the story.
  void onClose() {
    sessionShell.focusedStory?.delete();
  }

  /// Called when the keyboard help button is tapped.
  void onKeyboard() {
    if (helpVisibility.value == false) {
      // Close other system overlays.
      onCancel();
      helpVisibility.value = true;
      _keyboardListener.add(_cancelActionBinding);
    }
  }

  /// Called when the user initiates logout (using keyboard or UI).
  void onLogout() {
    onCancel();
    askChildViewConnection.value = null;
    _pointerEventsListener.stop();

    _componentControllerProxy.ctrl.close();
    _suggestionsService.ctrl.close();
    _ask.ctrl.close();
    status.dispose();
    _keyboardListener.close();
    sessionShell
      ..context.logout()
      ..stop();
  }

  void injectTap(Rect bounds) {
    final offset = bounds.topLeft;

    sessionShell.presentation.injectPointerEventHack(_createPointerEvent(
      phase: input.PointerEventPhase.add,
      offset: offset,
    ));

    sessionShell.presentation.injectPointerEventHack(_createPointerEvent(
      phase: input.PointerEventPhase.down,
      offset: offset,
    ));

    sessionShell.presentation.injectPointerEventHack(_createPointerEvent(
      phase: input.PointerEventPhase.up,
      offset: offset,
    ));

    sessionShell.presentation.injectPointerEventHack(_createPointerEvent(
      phase: input.PointerEventPhase.remove,
      offset: offset,
    ));
  }

  input.PointerEvent _createPointerEvent({
    input.PointerEventPhase phase,
    Offset offset,
  }) =>
      input.PointerEvent(
        eventTime: 0,
        deviceId: 0,
        pointerId: 0,
        type: input.PointerEventType.touch,
        phase: phase,
        x: offset.dx,
        y: offset.dy,
        buttons: 0,
      );

  bool _useInProcessStoryShell() {
    File file = File('/pkg/data/modular_config.json');
    if (file.existsSync()) {
      final data = json.decode(file.readAsStringSync());
      if (data is Map &&
          data['basemgr'] != null &&
          data['basemgr']['story_shell_url'] != null) {
        return false;
      }
    }
    return true;
  }
}
