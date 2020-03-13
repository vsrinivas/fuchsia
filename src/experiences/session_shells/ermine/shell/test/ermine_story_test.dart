// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

// ignore_for_file: implementation_imports
import 'package:ermine/src/models/ermine_story.dart';
import 'package:ermine/src/utils/presenter.dart';
import 'package:ermine/src/utils/suggestion.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

void main() {
  test('from suggestion sets id and title', () {
    final suggestion = Suggestion(id: 'id', title: 'title', url: 'url');
    final story = ErmineStory.fromSuggestion(
      suggestion: suggestion,
      launchFunction: (_, __) async {},
    );

    expect(story.id, 'id');
    expect(story.name, 'title');
  });

  test('Creating a story also launches the suggestion', () async {
    final completer = Completer<bool>();
    final suggestion = Suggestion(id: 'id', title: 'title', url: 'url');
    ErmineStory.fromSuggestion(
      suggestion: suggestion,
      launchFunction: (s, _) async => completer.complete(s.id == 'id'),
    );
    expect(await completer.future, true);
  });

  test('create from external source generates random id', () {
    final story1 = ErmineStory.fromExternalSource();
    final story2 = ErmineStory.fromExternalSource();

    expect(story1.id, isNot(story2.id));
  });

  test('Delete ErmineStory', () {
    final viewController = MockViewControllerImpl();
    bool didCallDelete = false;
    ErmineStory.fromExternalSource(onDelete: (_) => didCallDelete = true)
      ..viewController = viewController
      ..delete();

    expect(didCallDelete, isTrue);
    verify(viewController.close()).called(1);
  });
}

class MockViewControllerImpl extends Mock implements ViewControllerImpl {}
