// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert' show json;
import 'dart:io';

import 'package:fidl_fuchsia_app_discover/fidl_async.dart'
    show SuggestionsProxy;
import 'package:fidl_fuchsia_ui_input/fidl_async.dart' as input;

import 'package:flutter/material.dart';
import 'package:fuchsia_modular_flutter/session_shell.dart' show SessionShell;
import 'package:fuchsia_modular_flutter/story_shell.dart' show StoryShell;

import 'package:fuchsia_services/services.dart' show StartupContext;
import 'package:lib.widgets/utils.dart' show PointerEventsListener;

import '../utils/key_chord_listener.dart'
    show KeyChordListener, KeyChordBinding;
import '../utils/suggestions.dart';
import 'ask_model.dart';
import 'cluster_model.dart';
import 'status_model.dart';
import 'topbar_model.dart';

/// Model that manages all the application state of this session shell.
class AppModel {
  final _pointerEventsListener = PointerEventsListener();
  final _suggestionsService = SuggestionsProxy();
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

  ValueNotifier<bool> statusVisibility = ValueNotifier(false);
  ValueNotifier<bool> helpVisibility = ValueNotifier(false);
  ValueNotifier<bool> peekNotifier = ValueNotifier(false);
  KeyChordListener _keyboardListener;
  StatusModel status;
  AskModel askModel;
  TopbarModel topbarModel;
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

    topbarModel = TopbarModel(appModel: this);

    status = StatusModel.fromStartupContext(_startupContext);

    final suggestions = SuggestionsProxy();
    StartupContext.fromStartupInfo().incoming.connectToService(suggestions);

    askModel = AskModel(
      visibility: askVisibility,
      suggestionService: SuggestionService(suggestions),
    );
  }

  bool get isFullscreen => clustersModel.fullscreenStory != null;

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
      peekNotifier,
    ]).addListener(onCancel);
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
      askVisibility.value = true;
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
    askVisibility.value = false;
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
    _pointerEventsListener.stop();

    _suggestionsService.ctrl.close();
    askModel.dispose();
    status.dispose();
    _keyboardListener.close();
    sessionShell
      ..context.logout()
      ..stop();
  }

  void injectTap(Offset offset) {
    sessionShell.presentation
      ..injectPointerEventHack(_createPointerEvent(
        phase: input.PointerEventPhase.add,
        offset: offset,
      ))
      ..injectPointerEventHack(_createPointerEvent(
        phase: input.PointerEventPhase.down,
        offset: offset,
      ))
      ..injectPointerEventHack(_createPointerEvent(
        phase: input.PointerEventPhase.up,
        offset: offset,
      ))
      ..injectPointerEventHack(_createPointerEvent(
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
