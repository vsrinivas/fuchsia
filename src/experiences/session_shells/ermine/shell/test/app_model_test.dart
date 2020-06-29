// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:keyboard_shortcuts/keyboard_shortcuts.dart'
    show KeyboardShortcuts;
import 'package:fuchsia_internationalization_flutter/internationalization.dart';
import 'package:lib.widgets/utils.dart' show PointerEventsListener;
import 'package:test/test.dart';
import 'package:mockito/mockito.dart';

// ignore_for_file: implementation_imports
import 'package:ermine/src/models/cluster_model.dart';
import 'package:ermine/src/models/ermine_story.dart';
import 'package:ermine/src/models/status_model.dart';
import 'package:ermine/src/utils/suggestions.dart';
import 'package:ermine/src/models/app_model.dart';

void main() {
  AppModel appModel;
  KeyboardShortcuts keyboardShortcuts;
  PointerEventsListener pointerEventsListener;
  LocaleSource localeSource;
  SuggestionService suggestionService;
  StatusModel statusModel;
  ClustersModel clustersModel;

  setUp(() async {
    keyboardShortcuts = MockKeyboardShortcuts();
    pointerEventsListener = MockPointerEventsListener();
    localeSource = MockLocaleSource();
    suggestionService = MockSuggestionService();
    statusModel = MockStatusModel();
    clustersModel = MockClustersModel();

    appModel = _TestAppModel(
      keyboardShortcuts: keyboardShortcuts,
      pointerEventsListener: pointerEventsListener,
      localeSource: localeSource,
      suggestionService: suggestionService,
      statusModel: statusModel,
      clustersModel: clustersModel,
    );
    await appModel.onStarted();
  });

  tearDown(() {
    when(clustersModel.hasStories).thenReturn(false);

    appModel.onLogout();

    verify(keyboardShortcuts.dispose()).called(1);
    verify(pointerEventsListener.stop()).called(1);
    verify(suggestionService.dispose()).called(1);
    verify(statusModel.dispose()).called(1);
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
}

class _TestAppModel extends AppModel {
  _TestAppModel({
    KeyboardShortcuts keyboardShortcuts,
    PointerEventsListener pointerEventsListener,
    LocaleSource localeSource,
    SuggestionService suggestionService,
    StatusModel statusModel,
    ClustersModel clustersModel,
  }) : super(
          keyboardShortcuts: keyboardShortcuts,
          pointerEventsListener: pointerEventsListener,
          localeSource: localeSource,
          suggestionService: suggestionService,
          statusModel: statusModel,
          clustersModel: clustersModel,
        );

  @override
  void advertise() {}
}

class MockKeyboardShortcuts extends Mock implements KeyboardShortcuts {}

class MockPointerEventsListener extends Mock implements PointerEventsListener {}

class MockLocaleSource extends Mock implements LocaleSource {}

class MockSuggestionService extends Mock implements SuggestionService {}

class MockStatusModel extends Mock implements StatusModel {}

class MockClustersModel extends Mock implements ClustersModel {}

class MockErmineStory extends Mock implements ErmineStory {}
