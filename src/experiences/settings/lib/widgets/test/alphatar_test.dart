// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lib.testing.flutter/testing.dart';
import 'package:lib.widgets/widgets.dart';

void main() {
  String profileUrl =
      'https://raw.githubusercontent.com/dvdwasibi/DogsOfFuchsia/master/coco.jpg';

  testWidgets(
      'Alphatar should display the image when given, whether or not the '
      'fall-back letter is given, but also display fallback letter in '
      'the background', (WidgetTester tester) async {
    await HttpOverrides.runZoned(() async {
      // First, try without providing a letter.
      await tester.pumpWidget(
        MaterialApp(
          home: Material(
            child: Alphatar.withUrl(
              avatarUrl: profileUrl,
              retry: false,
            ),
          ),
        ),
      );

      expect(find.byType(Image), findsOneWidget);
      expect(find.byType(Icon), findsOneWidget);

      // Try again with a letter provided.
      await tester.pumpWidget(
        MaterialApp(
          home: Material(
            child: Alphatar.withUrl(
              avatarUrl: profileUrl,
              letter: 'L',
              retry: false,
            ),
          ),
        ),
      );

      expect(find.byType(Image), findsOneWidget);
      expect(find.byType(Text), findsOneWidget);
    }, createHttpClient: createFakeImageHttpClient);
  });

  testWidgets(
      'Alphatar should display the fall-back letter, '
      'when the image is not provided', (WidgetTester tester) async {
    await tester.pumpWidget(
      MaterialApp(
        home: Material(
          child: Alphatar(letter: 'L'),
        ),
      ),
    );

    expect(find.byType(Image), findsNothing);
    expect(find.text('L'), findsOneWidget);
  });

  test('Alphtars for the same name should have the same background color.', () {
    String name = 'John Doe';

    Alphatar a1 = Alphatar.fromName(name: name);
    Alphatar a2 = Alphatar.fromName(name: name);
    expect(a1.backgroundColor, equals(a2.backgroundColor));
  });
}
