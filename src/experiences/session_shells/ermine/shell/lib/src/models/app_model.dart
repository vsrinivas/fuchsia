// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:async/async.dart';
import 'package:fidl_fuchsia_intl/fidl_async.dart';
import 'package:fidl_fuchsia_ui_focus/fidl_async.dart';
import 'package:fidl_fuchsia_ui_input/fidl_async.dart' as input;
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_internationalization_flutter/internationalization.dart';
import 'package:fuchsia_inspect/inspect.dart' as inspect;
import 'package:fuchsia_services/services.dart' show StartupContext;
import 'package:keyboard_shortcuts/keyboard_shortcuts.dart'
    show KeyboardShortcuts;

import '../utils/focus_change_listener.dart';
import '../utils/pointer_events_stream.dart';
import '../utils/presenter.dart';
import '../utils/styles.dart';
import '../utils/suggestions.dart';
import '../widgets/ask/ask.dart';
import 'cluster_model.dart';
import 'status_model.dart';
import 'topbar_model.dart';

/// Model that manages all the application state of this session shell.
///
/// Its primary responsibility is to manage visibility of top level UI widgets
/// like Overview, Recents, Ask and Status.
class AppModel {
  KeyboardShortcuts _keyboardShortcuts;
  PointerEventsStream _pointerEventsStream;
  SuggestionService _suggestionService;

  final _intl = PropertyProviderProxy();

  PresenterService _presenterService;
  FocusChainListenerBinding _focusChainListenerBinding;

  /// The [GlobalKey] associated with [Ask] widget.
  final GlobalKey<AskState> askKey = GlobalKey(debugLabel: 'ask');

  final String backgroundImageUrl = 'assets/images/fuchsia.png';
  final Color backgroundColor = Colors.grey[850];
  final _startupContext = StartupContext.fromStartupInfo();

  final ValueNotifier<DateTime> currentTime =
      ValueNotifier<DateTime>(DateTime.now());
  ValueNotifier<bool> askVisibility = ValueNotifier(false);
  ValueNotifier<bool> overviewVisibility = ValueNotifier(true);
  ValueNotifier<bool> statusVisibility = ValueNotifier(false);
  ValueNotifier<bool> helpVisibility = ValueNotifier(false);
  ValueNotifier<bool> peekNotifier = ValueNotifier(false);
  ValueNotifier<bool> recentsVisibility = ValueNotifier(false);
  Stream<Locale> _localeStream;
  StreamSplitter<input.PointerEvent> _splitter;

  ClustersModel clustersModel;
  StatusModel statusModel;
  TopbarModel topbarModel;
  String keyboardShortcutsHelpText = 'Help Me!';

  AppModel({
    KeyboardShortcuts keyboardShortcuts,
    PointerEventsStream pointerEventsStream,
    LocaleSource localeSource,
    SuggestionService suggestionService,
    FocusChainListenerBinding focusChainListenerBinding,
    this.statusModel,
    this.clustersModel,
  })  : _keyboardShortcuts = keyboardShortcuts,
        _pointerEventsStream = pointerEventsStream,
        _focusChainListenerBinding = focusChainListenerBinding,
        _suggestionService = suggestionService {
    // Setup child models.
    topbarModel = TopbarModel(appModel: this);

    statusModel ??= StatusModel.fromStartupContext(_startupContext, onLogout);

    clustersModel ??= ClustersModel();

    // Setup keyboard shortcuts.
    _keyboardShortcuts ??= KeyboardShortcuts.fromStartupContext(
      _startupContext,
      actions: actions,
      bindings: keyboardBindings,
    );
    keyboardShortcutsHelpText = _keyboardShortcuts.helpText();

    // Setup pointer events listener.
    _pointerEventsStream ??=
        PointerEventsStream.fromStartupContext(_startupContext);
    _splitter = StreamSplitter(_pointerEventsStream.stream)
      ..split()
          .where((event) => event.phase == input.PointerEventPhase.move)
          .map((event) => Offset(event.x, event.y))
          .listen(_onPointerMove);

    // Setup locale stream.
    if (localeSource == null) {
      _startupContext.incoming.connectToService(_intl);
      localeSource = LocaleSource(_intl);
    }
    _localeStream = localeSource.stream().asBroadcastStream();

    // Suggestion service.
    _suggestionService ??= SuggestionService.fromStartupContext(
      startupContext: _startupContext,
      onSuggestion: clustersModel.storySuggested,
    );

    // Focus chain registry.
    if (_focusChainListenerBinding == null) {
      final focusChainRegistry = FocusChainListenerRegistryProxy();
      _startupContext.incoming.connectToService(focusChainRegistry);

      final listener = FocusChangeListener(onFocusChange);
      _focusChainListenerBinding = FocusChainListenerBinding();

      focusChainRegistry.register(_focusChainListenerBinding.wrap(listener));
      focusChainRegistry.ctrl.close();
    }

    // Expose PresenterService to the environment.
    advertise();
  }

  @visibleForTesting
  void advertise() {
    // Expose the presenter service to the environment.
    _presenterService = PresenterService(
      onPresent: clustersModel.presentStory,
      onDismiss: clustersModel.dismissStory,
    );
    _startupContext.outgoing
        .addPublicService(_presenterService.bind, PresenterService.serviceName);
  }

  SuggestionService get suggestions => _suggestionService;

  Stream<Locale> get localeStream => _localeStream;

  bool get isFullscreen => clustersModel.fullscreenStory != null;

  bool get hasStories => clustersModel.hasStories;

  /// Called after runApp which initializes flutter's gesture system.
  Future<void> onStarted() async {
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

  // Map key shortcuts to corresponding actions.
  Map<String, VoidCallback> get actions => {
        'shortcuts': onKeyboard,
        'ask': onAsk,
        'overview': onOverview,
        'recents': onRecents,
        'fullscreen': onFullscreen,
        'cancel': onCancel,
        'close': onClose,
        'status': onStatus,
        'nextCluster': clustersModel.nextCluster,
        'previousCluster': clustersModel.previousCluster,
        'logout': onLogout,
      };

  // Returns key bindings in keyboard_shortcuts.json. Throws a fatal exception
  // if not found.
  String get keyboardBindings {
    File file = File('/pkg/data/keyboard_shortcuts.json');
    return file.readAsStringSync();
  }

  void onFullscreen() {
    if (clustersModel.fullscreenStory != null) {
      clustersModel.fullscreenStory.restore();
    } else if (clustersModel.focusedStory != null) {
      clustersModel.maximize(clustersModel.focusedStory.id);
      // Hide system overlays.
      onCancel();
    }
  }

  /// Toggles the Ask bar when Overview is not visible.
  void onAsk() {
    if (!hasStories || overviewVisibility.value == true) {
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

  /// Toggles recents when Overview is not visible.
  void onRecents() {
    if (!hasStories || overviewVisibility.value == true) {
      return;
    }
    if (recentsVisibility.value == false) {
      // Close other system overlays.
      onCancel();
    }
    // Toggle recents visibility.
    recentsVisibility.value = !recentsVisibility.value;
  }

  /// Toggles the Status menu on/off when Overview is not visible.
  void onStatus() {
    if (!hasStories || overviewVisibility.value == true) {
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
    statusModel.reset();
    askVisibility.value = false;
    statusVisibility.value = false;
    helpVisibility.value = false;
    recentsVisibility.value = false;
    overviewVisibility.value = !hasStories;
  }

  /// Called when the user wants to delete the story.
  void onClose() {
    // Close is allowed when not in Overview.
    if (overviewVisibility.value == false) {
      clustersModel.focusedStory?.delete();
    }
  }

  /// Called when the keyboard help button is tapped.
  void onKeyboard() {
    if (overviewVisibility.value == true) {
      return;
    }
    if (helpVisibility.value == false) {
      // Close other system overlays.
      onCancel();
      helpVisibility.value = true;
    }
  }

  /// Returns true if any sysmtem overlay like [Ask], [Status], etc are visible.
  bool get overlaysVisible =>
      askVisibility.value || statusVisibility.value || helpVisibility.value;

  /// Called when the user initiates logout (using keyboard or UI).
  void onLogout() {
    onCancel();
    _keyboardShortcuts.dispose();
    _pointerEventsStream.dispose();
    _splitter.close();

    _intl?.ctrl?.close();
    _suggestionService.dispose();
    statusModel.dispose();
    _focusChainListenerBinding.close();
  }

  void _onInspect(inspect.Node node) {
    // Session.
    node.stringProperty('session').setValue('started');

    // Ask.
    askKey.currentState?.onInspect(node.child('ask'));

    // Status.
    statusModel.onInspect(node.child('status'));

    // Topbar.
    topbarModel.onInspect(node.child('topbar'));
  }

  void onFocusChange(List<ViewRef> focusedViews) {
    // Get the story whose [ViewRef] is in [focusedViews].
    final story = clustersModel.findStory(focusedViews);
    if (story != null) {
      story.focus();
      // Also dismiss any system overlays.
      onCancel();
    }
  }

  void _onPointerMove(Offset position) {
    if (isFullscreen && !overlaysVisible) {
      if (position.dy == 0) {
        peekNotifier.value = true;
      } else if (position.dy >
          ErmineStyle.kTopBarHeight + ErmineStyle.kStoryTitleHeight) {
        peekNotifier.value = false;
      }
    }
  }
}
