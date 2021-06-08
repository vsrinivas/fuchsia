// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: unused_import

import 'dart:async';
import 'dart:convert' show json;
import 'dart:io';

import 'package:async/async.dart';
import 'package:fidl_fuchsia_intl/fidl_async.dart';
import 'package:fidl_fuchsia_ui_focus/fidl_async.dart';
import 'package:fidl_fuchsia_ui_input/fidl_async.dart' as input;
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_inspect/inspect.dart';
import 'package:fuchsia_internationalization_flutter/internationalization.dart';
import 'package:fuchsia_scenic/views.dart';
import 'package:fuchsia_services/services.dart';
import 'package:keyboard_shortcuts/keyboard_shortcuts.dart'
    show KeyboardShortcuts;

import '../utils/focus_change_listener.dart';
import '../utils/pointer_events_stream.dart';
import '../utils/presenter.dart';
import '../utils/styles.dart';
import '../utils/suggestions.dart';
import '../widgets/ask/ask.dart';
import 'alert_model.dart';
import 'cluster_model.dart';
import 'status_model.dart';
import 'topbar_model.dart';

/// Model that manages all the application state of this session shell.
///
/// Its primary responsibility is to manage visibility of top level UI widgets
/// like Overview, Recents, Ask and Status.
class AppModel {
  Inspect _inspect;
  ComponentContext _componentContext;
  KeyboardShortcuts _keyboardShortcuts;
  PointerEventsStream _pointerEventsStream;
  SuggestionService _suggestionService;

  final _intl = PropertyProviderProxy();

  PresenterService _presenterService;
  FocusChainListenerBinding _focusChainListenerBinding;

  /// The [GlobalKey] associated with [Ask] widget.
  final GlobalKey<AskState> askKey = GlobalKey(debugLabel: 'ask');
  final GlobalKey<AskState> overviewAskKey =
      GlobalKey(debugLabel: 'overviewAsk');

  final String backgroundImageUrl = 'assets/images/fuchsia.png';
  final Color backgroundColor = Colors.grey[850];

  static const String dynamicConfigPath = '/data/startup_config.json';
  static const String staticConfigPath = '/config/data/startup_config.json';

  final ValueNotifier<DateTime> currentTime =
      ValueNotifier<DateTime>(DateTime.now());
  ValueNotifier<bool> alertVisibility = ValueNotifier(false);
  ValueNotifier<bool> askVisibility = ValueNotifier(false);
  ValueNotifier<bool> overviewVisibility = ValueNotifier(false);
  ValueNotifier<bool> oobeVisibility = ValueNotifier(true);
  ValueNotifier<bool> statusVisibility = ValueNotifier(false);
  ValueNotifier<bool> helpVisibility = ValueNotifier(false);
  ValueNotifier<bool> peekNotifier = ValueNotifier(false);
  ValueNotifier<bool> recentsVisibility = ValueNotifier(false);
  Stream<Locale> _localeStream;
  StreamSplitter<input.PointerEvent> _splitter;
  MethodChannel _flutterDriverHandler;

  AlertsModel alertsModel;
  ClustersModel clustersModel;
  StatusModel statusModel;
  TopbarModel topbarModel;
  String keyboardShortcutsHelpText = 'Help Me!';

  AppModel({
    ComponentContext componentContext,
    ViewRef viewRef,
    Inspect inspect,
    KeyboardShortcuts keyboardShortcuts,
    PointerEventsStream pointerEventsStream,
    LocaleSource localeSource,
    SuggestionService suggestionService,
    FocusChainListenerBinding focusChainListenerBinding,
    this.statusModel,
    this.clustersModel,
    this.alertsModel,
  })  : _componentContext = componentContext,
        _inspect = inspect,
        _keyboardShortcuts = keyboardShortcuts,
        _pointerEventsStream = pointerEventsStream,
        _focusChainListenerBinding = focusChainListenerBinding,
        _suggestionService = suggestionService {
    // Initialize ComponentContext.
    _componentContext ??= ComponentContext.create();
    final outgoing = _componentContext.outgoing;

    // Initialize ViewRef.
    viewRef ??= ScenicContext.hostViewRef();

    // Setup child models.
    topbarModel = TopbarModel(appModel: this);

    statusModel ??= StatusModel.withSvcPath(onLogout);

    alertsModel ??= AlertsModel();

    clustersModel ??= ClustersModel(onAlert: _onAlert);

    // Setup Inspect.
    _inspect ??= Inspect()..serve(outgoing);

    // Setup keyboard shortcuts.
    _keyboardShortcuts ??= KeyboardShortcuts.withViewRef(
      viewRef,
      actions: actions,
      bindings: keyboardBindings,
    );
    keyboardShortcutsHelpText = _keyboardShortcuts.helpText();

    // Load startup configuration. First check for dynamic config file, if it
    // is not there look for the static file, otherwise default to normal start.
    if (FileSystemEntity.typeSync(dynamicConfigPath) !=
        FileSystemEntityType.notFound) {
      _readStartupConfig(dynamicConfigPath);
    } else if (FileSystemEntity.typeSync(staticConfigPath) !=
        FileSystemEntityType.notFound) {
      _readStartupConfig(staticConfigPath);
    } else {
      oobeVisibility.value = false;
    }
    overviewVisibility.value = !oobeVisibility.value;

    // Setup pointer events listener.
    _pointerEventsStream ??= PointerEventsStream.withSvcPath();
    _splitter = StreamSplitter(_pointerEventsStream.stream)
      ..split()
          .where((event) => event.phase == input.PointerEventPhase.move)
          .map((event) => Offset(event.x, event.y))
          .listen(_onPointerMove);

    // Setup locale stream.
    if (localeSource == null) {
      Incoming.fromSvcPath().connectToService(_intl);
      localeSource = LocaleSource(_intl);
    }
    _localeStream = localeSource.stream().asBroadcastStream();

    // Suggestion service.
    _suggestionService ??= SuggestionService(
      onSuggestion: clustersModel.storySuggested,
    );

    // Focus chain registry.
    if (_focusChainListenerBinding == null) {
      final focusChainRegistry = FocusChainListenerRegistryProxy();
      Incoming.fromSvcPath().connectToService(focusChainRegistry);

      final listener = FocusChangeListener(onFocusChange);
      _focusChainListenerBinding = FocusChainListenerBinding();

      focusChainRegistry.register(_focusChainListenerBinding.wrap(listener));
      focusChainRegistry.ctrl.close();
    }

    // Expose PresenterService to the environment.
    advertise(outgoing);
  }

  @visibleForTesting
  void advertise(Outgoing outgoing) {
    // Expose the presenter service to the environment.
    _presenterService = PresenterService(
      onPresent: clustersModel.presentStory,
      onDismiss: clustersModel.dismissStory,
      onError: _onAlert,
    );
    outgoing
      ..addPublicService(_presenterService.bind, PresenterService.serviceName)
      ..serveFromStartupInfo();
  }

  SuggestionService get suggestions => _suggestionService;

  Stream<Locale> get localeStream => _localeStream;

  bool get isFullscreen => clustersModel.fullscreenStory != null;

  bool get hasStories => clustersModel.hasStories;

  /// Called after runApp which initializes flutter's gesture system.
  Future<void> onStarted() async {
    // We cannot load MaterialIcons font file from pubspec.yaml. So load it
    // explicitly.
    File file = File('/pkg/data/MaterialIcons-Regular.otf');
    if (file.existsSync()) {
      final loader = FontLoader('MaterialIcons')
        ..addFont(() async {
          return file.readAsBytesSync().buffer.asByteData();
        }());
      await loader.load();
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
    ]).addListener(() {
      onCancel();
      overviewVisibility.value = !hasStories;
    });

    alertsModel.addListener(onAlertChanged);

    Listenable.merge([
      statusVisibility,
      askVisibility,
      helpVisibility,
    ]).addListener(() {
      // Disable pointer hittesting on stories if overlays are visible.
      for (final story in clustersModel.stories) {
        story.hittestable = !overlaysVisible;
      }
    });

    // Add inspect data when requested.
    _inspect.onDemand('ermine', _onInspect);

    // Handle commands from Flutter Driver.
    _flutterDriverHandler = MethodChannel('flutter_driver/handler');
    _flutterDriverHandler.setMockMethodCallHandler((call) async {
      actions[call.method]?.call();
    });
  }

  // Map key shortcuts to corresponding actions.
  Map<String, VoidCallback> get actions => {
        'shortcuts': onKeyboard,
        'ask': onAsk,
        'overview': onOverview,
        'recents': onRecents,
        'fullscreen': onFullscreen,
        'cancel': onCancel,
        'clear': onClear,
        'close': onClose,
        'closeAll': onCloseAll,
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

  void _readStartupConfig(String filename) {
    File startupConfig = File(filename);
    final data = json.decode(startupConfig.readAsStringSync());

    // If configuration is not found, use default value false.
    oobeVisibility.value = data['launch_oobe'] ?? false;
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

  void onAlertChanged() {
    onCancel();
    alertVisibility.value = alertsModel.currentAlert != null;
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

  /// Clears the contents of the Ask bar.
  void onClear() {
    askKey.currentState?.model?.controller?.clear();
    overviewAskKey.currentState?.model?.controller?.clear();
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

  /// Exit OOBE and go to overview.
  void exitOobe() {
    // Set dynamic config file to false so that OOBE does not launch on next
    // boot.
    final configData = {'launch_oobe': false};
    final configString = json.encode(configData);
    File(dynamicConfigPath).writeAsStringSync(configString);

    oobeVisibility.value = false;
    overviewVisibility.value = true;
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
  }

  /// Called when the user wants to delete the story.
  void onClose() {
    // Close is allowed when not in Overview.
    if (overviewVisibility.value == false) {
      clustersModel.focusedStory?.delete();
    }
  }

  /// Called on 'closeAll' action.
  void onCloseAll() {
    while (clustersModel.stories.isNotEmpty) {
      clustersModel.stories.last.delete();
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
    statusModel.dispose();
    _focusChainListenerBinding.close();
  }

  void _onInspect(Node node) {
    // Session.
    node.stringProperty('session').setValue('started');

    // Clusters.
    clustersModel.onInspect(node.child('workspaces'));

    // Ask.
    askKey.currentState?.onInspect(node.child('ask'));

    // Status.
    statusModel.onInspect(node.child('status'));

    // Topbar.
    topbarModel.onInspect(node.child('topbar'));
  }

  void onFocusChange(List<ViewRef> focusedViews) {
    if (focusedViews == null) {
      return;
    }

    // Dismiss any system overlays.
    // TODO(https://fxbug.dev/47593): Don't dismiss overlay if focus is
    // switching to shell.
    onCancel();

    // Get the story whose [ViewRef] is in [focusedViews].
    final story = clustersModel.findStory(focusedViews);
    if (story != null && !story.focused) {
      story.focus();
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

  void _onAlert(String title, [String header = '', String description = '']) {
    final alert = AlertModel(
      alerts: alertsModel,
      header: header,
      title: title,
      description: description,
    );
    alertsModel.addAlert(alert);
  }
}
