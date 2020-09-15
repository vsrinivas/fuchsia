// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:ui';

import 'package:keyboard_shortcuts/keyboard_shortcuts.dart'
    show KeyboardShortcuts;
import 'package:fidl_fuchsia_ui_input/fidl_async.dart';
import 'package:fidl_fuchsia_ui_focus/fidl_async.dart';
import 'package:fuchsia_internationalization_flutter/internationalization.dart';
import 'package:test/test.dart';
import 'package:mockito/mockito.dart';

// ignore_for_file: implementation_imports
import 'package:ermine/src/models/cluster_model.dart';
import 'package:ermine/src/models/ermine_story.dart';
import 'package:ermine/src/models/status_model.dart';
import 'package:ermine/src/utils/pointer_events_stream.dart';
import 'package:ermine/src/utils/styles.dart';
import 'package:ermine/src/utils/suggestions.dart';
import 'package:ermine/src/models/app_model.dart';

void main() {
  AppModel appModel;
  KeyboardShortcuts keyboardShortcuts;
  PointerEventsStream pointerEventsStream;
  LocaleSource localeSource;
  SuggestionService suggestionService;
  StatusModel statusModel;
  ClustersModel clustersModel;
  StreamController<PointerEvent> controller;
  FocusChainListenerBinding focusChainListenerBinding;

  setUp(() async {
    keyboardShortcuts = MockKeyboardShortcuts();
    pointerEventsStream = MockPointerEventsStream();
    localeSource = MockLocaleSource();
    suggestionService = MockSuggestionService();
    statusModel = MockStatusModel();
    clustersModel = MockClustersModel();
    focusChainListenerBinding = MockFocusChainListenerBinding();
    controller = StreamController<PointerEvent>.broadcast();

    when(localeSource.stream()).thenAnswer((_) => Stream<Locale>.empty());
    when(pointerEventsStream.stream).thenAnswer((_) => controller.stream);

    appModel = _TestAppModel(
      keyboardShortcuts: keyboardShortcuts,
      pointerEventsStream: pointerEventsStream,
      localeSource: localeSource,
      suggestionService: suggestionService,
      statusModel: statusModel,
      clustersModel: clustersModel,
      focusChainListenerBinding: focusChainListenerBinding,
    );
    await appModel.onStarted();
  });

  tearDown(() {
    controller.close();
    when(clustersModel.hasStories).thenReturn(false);

    appModel.onLogout();

    verify(keyboardShortcuts.dispose()).called(1);
    verify(pointerEventsStream.dispose()).called(1);
    verify(suggestionService.dispose()).called(1);
    verify(statusModel.dispose()).called(1);
    verify(focusChainListenerBinding.close()).called(1);
  });

  test('Should start in Overview state', () async {
    expect(appModel.overviewVisibility.value, true);
    expect(appModel.askVisibility.value, false);
  });

  test('Toggle Overview state with or without stories', () async {
    when(clustersModel.hasStories).thenReturn(false);
    appModel.onOverview();
    expect(appModel.overviewVisibility.value, true);

    when(clustersModel.hasStories).thenReturn(true);
    appModel.onOverview();
    expect(appModel.overviewVisibility.value, false);
  });

  test('Should not toggle from Overview on Ask', () async {
    when(clustersModel.hasStories).thenReturn(true);

    appModel.overviewVisibility.value = true;
    appModel.onAsk();
    expect(appModel.askVisibility.value, false);
    expect(appModel.overviewVisibility.value, true);
  });

  test('Allow toggling Ask when NOT in Overview', () async {
    when(clustersModel.hasStories).thenReturn(true);
    appModel.overviewVisibility.value = false;

    appModel.onAsk();
    expect(appModel.askVisibility.value, true);
    appModel.onAsk();
    expect(appModel.askVisibility.value, false);
  });

  test('Allow toggling Recents when NOT in Overview', () async {
    when(clustersModel.hasStories).thenReturn(true);
    appModel.overviewVisibility.value = false;

    appModel.onRecents();
    expect(appModel.recentsVisibility.value, true);
    appModel.onRecents();
    expect(appModel.recentsVisibility.value, false);
  });

  test('Allow toggling Status when NOT in Overview', () async {
    when(clustersModel.hasStories).thenReturn(true);
    appModel.overviewVisibility.value = false;

    appModel.onStatus();
    expect(appModel.statusVisibility.value, true);
    appModel.onStatus();
    expect(appModel.statusVisibility.value, false);
  });

  test('Escape key should dismiss top level widgets.', () async {
    // When no stories are present, onCancel should display Overview.
    when(clustersModel.hasStories).thenReturn(false);
    appModel.onCancel();
    expect(appModel.overviewVisibility.value, true);
    expect(appModel.askVisibility.value, false);
    expect(appModel.statusVisibility.value, false);
    expect(appModel.helpVisibility.value, false);
    expect(appModel.recentsVisibility.value, false);

    // When stories are present, onCancel should not toggle Overview.
    when(clustersModel.hasStories).thenReturn(true);
    appModel.overviewVisibility.value = false;

    appModel.onCancel();
    expect(appModel.overviewVisibility.value, false);
    expect(appModel.askVisibility.value, false);
    expect(appModel.statusVisibility.value, false);
    expect(appModel.helpVisibility.value, false);
    expect(appModel.recentsVisibility.value, false);
  });

  test('Close should remove focused story.', () async {
    final story = MockErmineStory();
    when(clustersModel.focusedStory).thenReturn(story);

    // Close is not allowed in Overview.
    appModel.overviewVisibility.value = true;
    appModel.onClose();
    verifyNever(story.delete());

    // But allowed from cluster view.
    appModel.overviewVisibility.value = false;
    appModel.onClose();
    verify(story.delete()).called(1);
  });

  test('Keyboard help can be visible when not in Overview.', () async {
    when(clustersModel.hasStories).thenReturn(true);

    appModel.overviewVisibility.value = false;
    appModel.onKeyboard();
    expect(appModel.helpVisibility.value, true);
  });

  test('Should toggle fullscreen for focused story.', () async {
    final story = MockErmineStory();
    when(clustersModel.hasStories).thenReturn(true);
    when(clustersModel.focusedStory).thenReturn(story);

    appModel.onFullscreen();
    verify(clustersModel.maximize(any)).called(1);

    when(clustersModel.fullscreenStory).thenReturn(story);
    expect(appModel.isFullscreen, true);
    appModel.onFullscreen();
    verify(story.restore()).called(1);
  });

  test('Should hide Overview if stores are present', () async {
    when(clustersModel.hasStories).thenReturn(false);

    appModel.onCancel();
    expect(appModel.overviewVisibility.value, true);

    when(clustersModel.hasStories).thenReturn(true);

    appModel.onCancel();
    expect(appModel.overviewVisibility.value, false);
  });

  test('Should not go fullscreen if no story is in focus.', () async {
    appModel.onFullscreen();
    expect(appModel.isFullscreen, false);
  });

  test('Should dismiss overlays on tap', () async {
    when(clustersModel.hasStories).thenReturn(false);
    // Make ask overlay visible.
    appModel.askVisibility.value = true;

    final story = MockErmineStory();
    when(clustersModel.findStory(any)).thenReturn(story);

    appModel.onFocusChange([]);
    expect(appModel.askVisibility.value, false);
  });

  test('Should update peekNotifier when fullscreen', () async {
    final story = MockErmineStory();
    when(clustersModel.fullscreenStory).thenReturn(story);
    when(clustersModel.hasStories).thenReturn(true);

    expect(appModel.isFullscreen, true);

    appModel.peekNotifier.value = false;
    controller.add(_moveEvent(100, 0));
    await Future.delayed(Duration.zero);

    expect(appModel.peekNotifier.value, true);

    controller.add(_moveEvent(
        100, ErmineStyle.kTopBarHeight + ErmineStyle.kStoryTitleHeight + 1));
    await Future.delayed(Duration.zero);

    expect(appModel.peekNotifier.value, false);
  });
}

class _TestAppModel extends AppModel {
  _TestAppModel({
    KeyboardShortcuts keyboardShortcuts,
    PointerEventsStream pointerEventsStream,
    LocaleSource localeSource,
    SuggestionService suggestionService,
    StatusModel statusModel,
    ClustersModel clustersModel,
    FocusChainListenerBinding focusChainListenerBinding,
  }) : super(
          keyboardShortcuts: keyboardShortcuts,
          pointerEventsStream: pointerEventsStream,
          localeSource: localeSource,
          suggestionService: suggestionService,
          statusModel: statusModel,
          clustersModel: clustersModel,
          focusChainListenerBinding: focusChainListenerBinding,
        );

  @override
  void advertise() {}
}

PointerEvent _moveEvent(double x, double y) =>
    _pointerEvent(phase: PointerEventPhase.move, x: x, y: y);

PointerEvent _pointerEvent({PointerEventPhase phase, double x, double y}) =>
    PointerEvent(
      phase: phase,
      x: x,
      y: y,
      deviceId: 0,
      type: PointerEventType.mouse,
      buttons: 0,
      eventTime: 0,
      pointerId: 0,
    );

class MockKeyboardShortcuts extends Mock implements KeyboardShortcuts {}

class MockPointerEventsStream extends Mock implements PointerEventsStream {}

class MockLocaleSource extends Mock implements LocaleSource {}

class MockSuggestionService extends Mock implements SuggestionService {}

class MockStatusModel extends Mock implements StatusModel {}

class MockClustersModel extends Mock implements ClustersModel {}

class MockErmineStory extends Mock implements ErmineStory {}

class MockFocusChainListenerBinding extends Mock
    implements FocusChainListenerBinding {}
