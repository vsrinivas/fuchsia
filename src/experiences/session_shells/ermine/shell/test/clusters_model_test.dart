// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:fuchsia_scenic_flutter/child_view_connection.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

// ignore_for_file: implementation_imports
import 'package:ermine/src/models/cluster_model.dart';
import 'package:ermine/src/utils/presenter.dart';
import 'package:ermine/src/utils/suggestion.dart';

void main() {
  ClustersModel clustersModel;

  setUp(() {
    clustersModel = ClustersModel();
  });

  test('Story from suggestion should have focus', () {
    final suggestion = Suggestion(id: 'id', url: 'url', title: 'title');
    clustersModel.storySuggested(suggestion, (_, __) async {});

    expect(clustersModel.hasStories, true);
    expect(clustersModel.focusedStory, isNotNull);
  });

  test('Story from presentation should have focus', () {
    final eventPair = MockEventPair();
    when(eventPair.isValid).thenReturn(false);

    clustersModel.presentStory(
      MockChildViewConnection(),
      ViewRef(reference: eventPair),
      ViewControllerImpl((_) {}),
      'id',
    );

    expect(clustersModel.hasStories, true);
    expect(clustersModel.focusedStory, isNotNull);

    final id = clustersModel.focusedStory.id;
    expect(clustersModel.getStory(id), isNotNull);
  });

  test('Dismissed stories should be deleted', () {
    final eventPair = MockEventPair();
    when(eventPair.isValid).thenReturn(false);

    final viewController = ViewControllerImpl((_) {});
    clustersModel.presentStory(
      MockChildViewConnection(),
      ViewRef(reference: eventPair),
      viewController,
      'id',
    );

    final story = clustersModel.focusedStory;
    expect(story.viewController, viewController);

    clustersModel.dismissStory(viewController);
    expect(clustersModel.hasStories, false);
  });

  test('Story maximize/minimize should toggle fullscreen', () {
    // Create a story.
    final suggestion = Suggestion(id: 'id', url: 'url', title: 'title');
    clustersModel.storySuggested(suggestion, (_, __) async {});
    final story = clustersModel.focusedStory;

    clustersModel.maximize(story.id);

    expect(story.fullscreen, true);
    expect(clustersModel.fullscreenStoryNotifier.value, story);

    clustersModel.restore(story.id);

    expect(story.fullscreen, false);
    expect(clustersModel.fullscreenStoryNotifier.value, isNull);
  });

  test('Story delete should remove from cluster', () {
    // Create a story.
    final suggestion = Suggestion(id: 'id', url: 'url', title: 'title');
    clustersModel.storySuggested(suggestion, (_, __) async {});
    expect(clustersModel.getStory('id'), isNotNull);

    final story = clustersModel.focusedStory;
    clustersModel.storyDeleted(story);

    expect(clustersModel.getStory('id'), isNull);
  });

  test('Setting focus on story should defocus previous story', () {
    // Create fisrt story.
    var suggestion = Suggestion(id: 'id-1', url: 'url', title: 'title');
    clustersModel.storySuggested(suggestion, (_, __) async {});

    final story1 = clustersModel.getStory('id-1');
    expect(clustersModel.focusedStory, story1);

    // Create second story.
    suggestion = Suggestion(id: 'id-2', url: 'url', title: 'title');
    clustersModel.storySuggested(suggestion, (_, __) async {});

    final story2 = clustersModel.getStory('id-2');
    expect(clustersModel.focusedStory, story2);
    expect(story1.focused, false);

    // Change story1 by setting focus on it.
    story1.focus();
    expect(clustersModel.focusedStory, story1);
    expect(story1.focused, true);
    expect(story2.focused, false);
  });

  test('Moving to next cluster should change current cluster', () {
    // Create fisrt story.
    var suggestion = Suggestion(id: 'id-1', url: 'url', title: 'title');
    clustersModel.storySuggested(suggestion, (_, __) async {});

    final firstCluster = clustersModel.currentCluster.value;
    clustersModel.nextCluster();

    expect(clustersModel.currentCluster.value, isNot(firstCluster));

    clustersModel.previousCluster();
    expect(clustersModel.currentCluster.value, firstCluster);
  });
}

class MockChildViewConnection extends Mock implements ChildViewConnection {}

class MockEventPair extends Mock implements EventPair {}
