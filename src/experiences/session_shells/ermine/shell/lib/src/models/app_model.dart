// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert' show json;
import 'dart:io';

import 'package:fidl_fuchsia_intl/fidl_async.dart';
import 'package:fidl_fuchsia_ui_input/fidl_async.dart' as input;
import 'package:fidl_fuchsia_ui_shortcut/fidl_async.dart' as ui_shortcut
    show RegistryProxy;

import 'package:flutter/material.dart';
import 'package:fuchsia_internationalization_flutter/internationalization.dart';
import 'package:fuchsia_inspect/inspect.dart' as inspect;
import 'package:fuchsia_modular_flutter/session_shell.dart' show SessionShell;
import 'package:fuchsia_modular_flutter/story_shell.dart' show StoryShell;

import 'package:fuchsia_services/services.dart' show StartupContext;
import 'package:keyboard_shortcuts/keyboard_shortcuts.dart'
    show KeyboardShortcuts;
import 'package:lib.widgets/utils.dart' show PointerEventsListener;

import '../utils/suggestions.dart';
import '../widgets/ask/ask.dart';
import 'cluster_model.dart';
import 'status_model.dart';
import 'topbar_model.dart';

/// Model that manages all the application state of this session shell.
class AppModel {
  final _pointerEventsListener = PointerEventsListener();
  final _shortcutRegistry = ui_shortcut.RegistryProxy();
  final _intl = PropertyProviderProxy();

  SessionShell sessionShell;
  SuggestionService _suggestionService;

  /// The [GlobalKey] associated with [Ask] widget.
  final GlobalKey<AskState> askKey = GlobalKey(debugLabel: 'ask');

  final String backgroundImageUrl = 'assets/images/fuchsia.png';
  final Color backgroundColor = Colors.grey[850];
  final _startupContext = StartupContext.fromStartupInfo();

  final ClustersModel clustersModel = ClustersModel();
  final ValueNotifier<DateTime> currentTime =
      ValueNotifier<DateTime>(DateTime.now());
  ValueNotifier<bool> askVisibility = ValueNotifier(false);
  ValueNotifier<bool> overviewVisibility = ValueNotifier(true);
  ValueNotifier<bool> statusVisibility = ValueNotifier(false);
  ValueNotifier<bool> helpVisibility = ValueNotifier(false);
  ValueNotifier<bool> peekNotifier = ValueNotifier(false);
  ValueNotifier<bool> recentsVisibility = ValueNotifier(false);
  Stream<Locale> _localeStream;
  KeyboardShortcuts _keyboardShortcuts;
  StatusModel status;
  TopbarModel topbarModel;
  String keyboardShortcuts = 'Help Me!';

  AppModel() {
    StartupContext.fromStartupInfo()
        .incoming
        .connectToService(_shortcutRegistry);

    StartupContext.fromStartupInfo().incoming.connectToService(_intl);
    _localeStream = LocaleSource(_intl).stream().asBroadcastStream();

    _suggestionService =
        SuggestionService.fromStartupContext(StartupContext.fromStartupInfo());

    sessionShell = SessionShell(
      startupContext: _startupContext,
      onStoryStarted: clustersModel.addStory,
      onStoryDeleted: clustersModel.removeStory,
      onStoryChanged: clustersModel.changeStory,
    )..start();

    clustersModel.useInProcessStoryShell = _useInProcessStoryShell();
    if (clustersModel.useInProcessStoryShell) {
      StoryShell.advertise(
        startupContext: _startupContext,
        onStoryAttached: clustersModel.getStory,
      );
    }

    topbarModel = TopbarModel(appModel: this);

    status = StatusModel.fromStartupContext(_startupContext, onLogout);
  }

  SuggestionService get suggestions => _suggestionService;

  Stream<Locale> get localeStream => _localeStream;

  bool get isFullscreen => clustersModel.fullscreenStory != null;

  bool get hasStories => clustersModel.hasStories;

  /// Called after runApp which initializes flutter's gesture system.
  Future<void> onStarted() async {
    // Capture pointer events directly from Scenic.
    _pointerEventsListener.listen(sessionShell.presentation);

    // Capture key pressess for key bindings in keyboard_shortcuts.json.
    File file = File('/pkg/data/keyboard_shortcuts.json');
    if (file.existsSync()) {
      final bindings = await file.readAsString();
      _keyboardShortcuts = KeyboardShortcuts(
        registry: _shortcutRegistry,
        actions: {
          'shortcuts': onKeyboard,
          'ask': onMeta,
          'overview': onOverview,
          'recents': onRecents,
          'fullscreen': onFullscreen,
          'cancel': onCancel,
          'close': onClose,
          'status': onStatus,
          'nextCluster': clustersModel.nextCluster,
          'previousCluster': clustersModel.previousCluster,
          'logout': onLogout,
        },
        bindings: bindings,
      );
      keyboardShortcuts = _keyboardShortcuts.helpText();
    } else {
      throw ArgumentError.value(
          'keyboard_shortcuts.json', 'fileName', 'File does not exist');
    }

    // Update the current time every second.
    Timer.periodic(
        Duration(seconds: 1), (timer) => currentTime.value = DateTime.now());

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

    // Add inspect data when requested.
    inspect.Inspect.onDemand('ermine', _onInspect);
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

  /// Toggles the Ask bar.
  void onMeta() {
    if (!hasStories) {
      return;
    }
    if (askVisibility.value == false) {
      // Close other system overlays.
      onCancel();
    }
    askVisibility.value = !askVisibility.value;
  }

  /// Toggles overview.
  void onOverview() {
    if (!hasStories) {
      return;
    }
    if (overviewVisibility.value == false) {
      // Close other system overlays.
      onCancel();
    }
    // Toggle overview visibility.
    overviewVisibility.value = !overviewVisibility.value;
  }

  /// Toggles recents.
  void onRecents() {
    if (!hasStories) {
      return;
    }
    if (recentsVisibility.value == false) {
      // Close other system overlays.
      onCancel();
    }
    // Toggle recents visibility.
    recentsVisibility.value = !recentsVisibility.value;
  }

  /// Toggles the Status menu on/off.
  void onStatus() {
    if (!hasStories) {
      return;
    }
    if (statusVisibility.value == false) {
      // Close other system overlays.
      onCancel();
    }
    statusVisibility.value = !statusVisibility.value;
  }

  /// Called when tapped behind Ask bar, quick settings, notifications or the
  /// Escape key was pressed.
  void onCancel() {
    status.reset();
    askVisibility.value = false;
    statusVisibility.value = false;
    helpVisibility.value = false;
    recentsVisibility.value = false;
    overviewVisibility.value = !hasStories;
  }

  /// Called when the user wants to delete the story.
  void onClose() {
    sessionShell.focusedStory?.delete();
  }

  /// Called when the keyboard help button is tapped.
  void onKeyboard() {
    if (!hasStories) {
      return;
    }
    if (helpVisibility.value == false) {
      // Close other system overlays.
      onCancel();
      helpVisibility.value = true;
    }
  }

  /// Called when the user initiates logout (using keyboard or UI).
  void onLogout() {
    onCancel();
    _pointerEventsListener.stop();

    _intl.ctrl.close();
    _suggestionService.dispose();
    status.dispose();
    _keyboardShortcuts.dispose();
    _shortcutRegistry.ctrl.close();
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

  void _onInspect(inspect.Node node) {
    // Session.
    node.stringProperty('session').setValue('started');

    // Ask.
    askKey.currentState?.onInspect(node.child('ask'));

    // Status.
    status.onInspect(node.child('status'));

    // Topbar.
    topbarModel.onInspect(node.child('topbar'));
  }
}
