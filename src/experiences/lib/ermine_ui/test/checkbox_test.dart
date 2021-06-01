// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_ui/ermine_ui.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

// ignore_for_file: avoid_as
void main() {
  testWidgets('Verify the color of active checkbox',
      (WidgetTester tester) async {
    final activeCheckbox = ErmineCheckbox(value: true, onChanged: (value) {});
    await tester.pumpWidget(_buildCheckbox(activeCheckbox));

    final checkbox = tester.firstWidget(find.byType(Checkbox)) as Checkbox;

    expect(checkbox.checkColor, ErmineColors.grey100);
    expect(checkbox.activeColor, Colors.transparent);

    final container = tester.firstWidget(find.byType(Container)) as Container;
    final decoration = container.decoration as BoxDecoration;
    final border = decoration.border as Border;
    final borderSide = border.top;
    expect(borderSide.color, ErmineColors.grey100);
  });

  testWidgets('Verify the color of disabled checkbox',
      (WidgetTester tester) async {
    final disabledCheckbox = ErmineCheckbox(value: true);
    await tester.pumpWidget(_buildCheckbox(disabledCheckbox));

    final checkbox = tester.firstWidget(find.byType(Checkbox)) as Checkbox;

    expect(checkbox.checkColor, ErmineColors.grey300);
    expect(checkbox.activeColor, Colors.transparent);

    final container = tester.firstWidget(find.byType(Container)) as Container;
    final decoration = container.decoration as BoxDecoration;
    final border = decoration.border as Border;
    final borderSide = border.top;
    expect(borderSide.color, ErmineColors.grey300);
  });
}

Widget _buildCheckbox(ErmineCheckbox checkbox) => MaterialApp(
      home: Scaffold(
        body: checkbox,
      ),
    );
