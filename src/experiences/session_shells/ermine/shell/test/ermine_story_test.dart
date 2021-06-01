// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

// ignore_for_file: implementation_imports
import 'package:ermine/src/models/ermine_story.dart';
import 'package:ermine/src/utils/presenter.dart';
import 'package:ermine/src/utils/suggestion.dart';
import 'package:fidl_fuchsia_session/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/foundation.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

void main() {
  test('from suggestion sets id and title', () {
    final suggestion = Suggestion(id: 'id', title: 'title', url: 'url');
    final story = ErmineStory.fromSuggestion(
      suggestion: suggestion,
      launchSuggestion: (_, __, ___) async {},
    );

    expect(story.id, 'id');
    expect(story.name, 'title');
  });

  test('Creating a story also launches the suggestion', () async {
    final completer = Completer<bool>();
    final suggestion = Suggestion(id: 'id', title: 'title', url: 'url');
    ErmineStory.fromSuggestion(
      suggestion: suggestion,
      launchSuggestion: (s, _, ___) async => completer.complete(s.id == 'id'),
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
    when(viewController.viewRendered).thenReturn(ValueNotifier(true));
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
    final fuchsiaViewConnection = MockFuchsiaViewConnection();
    final viewController = MockViewControllerImpl();

    when(viewRef.reference).thenReturn(eventPair);
    when(eventPair.duplicate(ZX.RIGHT_SAME_RIGHTS)).thenReturn(eventPairDup);
    when(eventPairDup.isValid).thenReturn(true);
    when(viewController.stateChanged).thenReturn(ValueNotifier<bool>(true));

    bool onChangeCalled = false;
    final completer = Completer<bool>();
    TestErmineStory(
      onChange: (_) => onChangeCalled = true,
      requestFocusCompleter: completer,
    )
      ..viewRef = viewRef
      ..fuchsiaViewConnectionNotifier.value = fuchsiaViewConnection
      ..viewController = viewController
      ..focus();

    expect(onChangeCalled, true);
    await completer.future;

    verify(fuchsiaViewConnection.requestFocus(0)).called(1);
  });

  test('ErmineStory should handle errors caught while proposing an element',
      () async {
    var _title = '';
    var _header = '';
    var _description = '';

    void onError(String title, [String header, String description]) {
      _title = title;
      _header = header;
      _description = description;
    }

    const title = 'An error occurred while launching';
    ErmineStory.handleError(ProposeElementError.notFound, 'element1', onError);
    expect(_title, equals(title));
    expect(_header, equals('element1'));
    expect(
        _description,
        equals('ElementProposeError.NOT_FOUND:\n'
            'The component URL could not be resolved.'));

    ErmineStory.handleError(ProposeElementError.rejected, 'element2', onError);
    expect(_title, equals(title));
    expect(_header, equals('element2'));
    expect(
        _description,
        equals('ElementProposeError.REJECTED:\n'
            'The element spec may have been malformed.'));

    const arbitraryError = 'This is an arbitrary type of error.';
    ErmineStory.handleError(arbitraryError, 'element3', onError);
    expect(_title, equals(title));
    expect(_header, equals('element3'));
    expect(_description, equals(arbitraryError));
  });
}

class TestErmineStory extends ErmineStory {
  final Completer<bool> requestFocusCompleter;

  TestErmineStory({
    void Function(ErmineStory) onChange,
    this.requestFocusCompleter,
  }) : super(id: 'id', onChange: onChange);

  @override
  Future<void> requestFocus() async {
    final result = await super.requestFocus();
    requestFocusCompleter.complete(true);
    return result;
  }
}

class MockViewControllerImpl extends Mock implements ViewControllerImpl {}

class MockFuchsiaViewConnection extends Mock implements FuchsiaViewConnection {}

class MockViewRef extends Mock implements ViewRef {}

class MockEventPair extends Mock implements EventPair {}
