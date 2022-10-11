// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_ui/ermine_ui.dart';
// flutter has its own FilledButton component, this conflicts with the one in ermine_ui
import 'package:flutter/material.dart' hide FilledButton;
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('Alert should display given information correctly.',
      (WidgetTester tester) async {
    var isClosed = false;
    final widget =
        Container(width: 50, height: 50, key: ValueKey('custon_widget'));
    final buttons = [
      BorderedButton.small('btn 1', () {}),
      FilledButton.small('btn 2', () {})
    ];

    const header = 'Header';
    const title = 'Title';
    const description = 'This is an alert.';

    // Creates an alert with all fields.
    final alert = Alert(
      header: header,
      title: title,
      description: description,
      customWidget: widget,
      buttons: buttons,
      onClose: () => isClosed = true,
    );

    await tester.pumpWidget(_buildAlert(alert));

    // All widgets should be found.
    expect(find.text(header), findsOneWidget);
    expect(find.text(title.toUpperCase()), findsOneWidget);
    expect(find.text(description), findsOneWidget);
    expect(find.byKey(ValueKey('custon_widget')), findsOneWidget);
    expect(find.byKey(ValueKey('alert_close')), findsOneWidget);
    expect(find.byKey(ValueKey('alert_divider')), findsOneWidget);
    expect(find.byType(BorderedButton), findsOneWidget);
    expect(find.byType(FilledButton), findsOneWidget);

    // The close icon should work.
    expect(isClosed, isFalse);
    await tester.tap(find.byKey(ValueKey('alert_close')));
    expect(isClosed, isTrue);
  });

  testWidgets('Alert should not display unnecessary widgets.',
      (WidgetTester tester) async {
    const title = 'Title';

    // Creates an alert with only the required fields.
    final alert = Alert(
      title: title,
      onClose: () {},
    );

    await tester.pumpWidget(_buildAlert(alert));

    // A close icon should be found.
    expect(find.byKey(ValueKey('alert_close')), findsOneWidget);

    // A title should be found.
    expect(find.text(title.toUpperCase()), findsOneWidget);

    // header, divider, description, and button row should not be found.
    expect(find.byKey(ValueKey('alert_header')), findsNothing);
    expect(find.byKey(ValueKey('alert_divider')), findsNothing);
    expect(find.byKey(ValueKey('alert_description')), findsNothing);
    expect(find.byKey(ValueKey('alert_button_row')), findsNothing);
  });

  testWidgets('The alert header can be displayed up to one line.',
      (WidgetTester tester) async {
    const header = 'very long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long header';
    const title = 'Title';

    // Creates an alert with a very long header.
    final alert = Alert(
      header: header,
      title: title,
      onClose: () {},
      key: ValueKey('alert'),
    );

    await tester.pumpWidget(_buildAlert(alert));

    // The alert should not exceed the max width limit, 816.0.
    final alertSize = tester.getSize(find.byKey(ValueKey('alert')));
    expect(alertSize.width, lessThanOrEqualTo(816.0));

    // The header should be one line.
    final headerSize = tester.getSize(find.byKey(ValueKey('alert_header')));
    expect(headerSize.height, lessThanOrEqualTo(24.0));
  });

  testWidgets('The alert title can be displayed up to three lines.',
      (WidgetTester tester) async {
    const header = 'Header';
    const title = 'Very long long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long title';

    // Creates an alert with a very long title.
    final alert = Alert(
      header: header,
      title: title,
      onClose: () {},
      key: ValueKey('alert'),
    );

    await tester.pumpWidget(_buildAlert(alert));

    // The alert should not exceed the max width limit, 816.0.
    final alertSize = tester.getSize(find.byKey(ValueKey('alert')));
    expect(alertSize.width, lessThanOrEqualTo(816.0));

    // The title should be three lines.
    final titleHeight =
        tester.getSize(find.byKey(ValueKey('alert_title'))).height;
    expect(titleHeight, greaterThan(28 * 2));
    expect(titleHeight, lessThanOrEqualTo(28 * 3));
  });

  testWidgets('The alert description can be displayed up to five lines.',
      (WidgetTester tester) async {
    const header = 'Header';
    const title = 'Title';
    const description = 'Very long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long long \n'
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long long long long '
        'long long long long long long long long long long long description';

    // Creates an alert with a very long description.
    final alert = Alert(
      header: header,
      title: title,
      description: description,
      onClose: () {},
      key: ValueKey('alert'),
    );

    await tester.pumpWidget(_buildAlert(alert));

    // The alert should not exceed the max width limit, 816.0.
    final alertSize = tester.getSize(find.byKey(ValueKey('alert')));
    expect(alertSize.width, lessThanOrEqualTo(816.0));

    // The title should be three lines.
    final descHeight =
        tester.getSize(find.byKey(ValueKey('alert_description'))).height;
    expect(descHeight, greaterThan(28 * 4));
    expect(descHeight, lessThanOrEqualTo(28 * 5));
  });
}

Widget _buildAlert(Alert alert) => MaterialApp(
      home: Scaffold(
        body: alert,
      ),
    );
