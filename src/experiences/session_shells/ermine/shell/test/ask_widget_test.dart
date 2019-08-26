// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart'
    show RawKeyDownEvent, RawKeyEventDataFuchsia;
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';

// ignore_for_file: implementation_imports
import 'package:ermine_library/src/models/ask_model.dart';
import 'package:ermine_library/src/utils/suggestions.dart';
import 'package:ermine_library/src/widgets/ask/ask.dart';

void main() {
  testWidgets('Create Ask Widget', (tester) async {
    final suggestionService = MockSuggestionService();
    final model = AskModel(
      visibility: ValueNotifier<bool>(false),
      suggestionService: suggestionService,
    );

    final widget = MaterialApp(home: Ask(model: model));
    await tester.pumpWidget(widget);

    final hintFinder = find.text('TYPE TO ASK');
    expect(hintFinder, findsOneWidget);
  });

  testWidgets('Hide Ask Widget', (tester) async {
    final suggestionService = MockSuggestionService();
    final model = AskModel(
      visibility: ValueNotifier<bool>(false),
      suggestionService: suggestionService,
    );

    final widget = MaterialApp(home: Ask(model: model));
    await tester.pumpWidget(widget);

    // Press Esc.
    model.handleKey(RawKeyDownEvent(
      data: RawKeyEventDataFuchsia(
        hidUsage: AskModel.kEsc,
      ),
    ));
    expect(model.visibility.value, false);
  });

  testWidgets('Hide hint text on typing', (tester) async {
    final suggestionService = MockSuggestionService();
    final model = AskModel(
      visibility: ValueNotifier<bool>(false),
      suggestionService: suggestionService,
    );

    final widget = MaterialApp(home: Ask(model: model));
    await tester.pumpWidget(widget);

    final textFieldFinder = find.byType(TextField);
    await tester.enterText(textFieldFinder, 'hello');
    await tester.pumpAndSettle();

    final hintFinder = find.text('TYPE TO ASK');
    expect(hintFinder, findsNothing);
  });

  testWidgets('Displays suggestions', (tester) async {
    final suggestionService = MockSuggestionService();
    final model = AskModel(
      visibility: ValueNotifier<bool>(false),
      suggestionService: suggestionService,
    );

    final widget = MaterialApp(home: Ask(model: model));
    await tester.pumpWidget(widget);

    when(suggestionService.getSuggestions('hello'))
        .thenAnswer((_) => Future<Iterable<Suggestion>>.value(<Suggestion>[
              Suggestion(id: 'one', displayInfo: DisplayInfo(title: 'hi')),
              Suggestion(id: 'two', displayInfo: DisplayInfo(title: 'there')),
            ]));

    final completer = Completer();
    model.suggestions.addListener(completer.complete);

    final textFieldFinder = find.byType(TextField);
    await tester.enterText(textFieldFinder, 'hello');
    await tester.pumpAndSettle();

    await completer.future;

    final suggestionFinder = find.text('hi');
    expect(suggestionFinder, findsOneWidget);
  });
}

// Mock classes.
class MockSuggestionService extends Mock implements SuggestionService {}
