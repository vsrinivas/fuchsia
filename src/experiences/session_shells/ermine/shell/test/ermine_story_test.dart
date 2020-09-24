// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

// ignore_for_file: implementation_imports
import 'package:ermine/src/models/ermine_story.dart';
import 'package:ermine/src/utils/presenter.dart';
import 'package:ermine/src/utils/suggestion.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:fuchsia_scenic_flutter/child_view_connection.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

void main() {
  test('from suggestion sets id and title', () {
    final suggestion = Suggestion(id: 'id', title: 'title', url: 'url');
    final story = ErmineStory.fromSuggestion(
      suggestion: suggestion,
      launchSuggestion: (_, __) async {},
    );

    expect(story.id, 'id');
    expect(story.name, 'title');
  });

  test('Creating a story also launches the suggestion', () async {
    final completer = Completer<bool>();
    final suggestion = Suggestion(id: 'id', title: 'title', url: 'url');
    ErmineStory.fromSuggestion(
      suggestion: suggestion,
      launchSuggestion: (s, _) async => completer.complete(s.id == 'id'),
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

  test('Focus ErmineStory', () async {
    final eventPair = MockEventPair();
    final eventPairDup = MockEventPair();
    final viewRef = MockViewRef();
    final viewRefInstalled = MockViewRefInstalled();
    final childViewConnection = MockChildViewConnection();

    when(viewRef.reference).thenReturn(eventPair);
    when(eventPair.duplicate(ZX.RIGHT_SAME_RIGHTS)).thenReturn(eventPairDup);
    when(eventPairDup.isValid).thenReturn(true);

    bool onChangeCalled = false;
    final completer = Completer<bool>();
    TestErmineStory(
      onChange: (_) => onChangeCalled = true,
      requestFocusCompleter: completer,
    )
      ..viewRef = viewRef
      ..childViewConnectionNotifier.value = childViewConnection
      ..focus(viewRefInstalled);

    expect(onChangeCalled, true);
    await completer.future;

    verify(viewRefInstalled.watch(ViewRef(reference: eventPairDup))).called(1);
    verify(childViewConnection.requestFocus()).called(1);
  });
}

class TestErmineStory extends ErmineStory {
  final Completer<bool> requestFocusCompleter;

  TestErmineStory({
    void Function(ErmineStory) onChange,
    this.requestFocusCompleter,
  }) : super(id: 'id', onChange: onChange);

  @override
  Future<void> requestFocus([ViewRefInstalledProxy viewRefInstalled]) async {
    final result = await super.requestFocus(viewRefInstalled);
    requestFocusCompleter.complete(true);
    return result;
  }
}

class MockViewControllerImpl extends Mock implements ViewControllerImpl {}

class MockChildViewConnection extends Mock implements ChildViewConnection {}

class MockViewRefInstalled extends Mock implements ViewRefInstalledProxy {}

class MockViewRef extends Mock implements ViewRef {}

class MockEventPair extends Mock implements EventPair {}
