// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:a11y_demo/main.dart';

void main() {
  testWidgets('Counters increment', (WidgetTester tester) async {
    await tester.pumpWidget(Directionality(
      textDirection: TextDirection.ltr,
      child: SimpleButtonsAndLabelPage(),
    ));

    expect(find.text('Blue tapped 0 times'), findsOneWidget);
    expect(find.text('Yellow tapped 0 times'), findsOneWidget);
    expect(find.text('Blue'), findsOneWidget);
    expect(find.text('Yellow'), findsOneWidget);

    await tester.tap(find.text('Blue'));
    await tester.pump();

    expect(find.text('Blue tapped 1 time'), findsOneWidget);
    expect(find.text('Yellow tapped 0 times'), findsOneWidget);

    await tester.tap(find.text('Yellow'));
    await tester.pump();

    expect(find.text('Blue tapped 1 time'), findsOneWidget);
    expect(find.text('Yellow tapped 1 time'), findsOneWidget);
  });
}
