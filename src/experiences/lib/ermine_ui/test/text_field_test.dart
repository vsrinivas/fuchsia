// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_ui/ermine_ui.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

const parentWidth = 500.0;

void main() {
  final controller = TextEditingController();

  testWidgets('Should have a label when one is given.',
      (WidgetTester tester) async {
    const label = 'Hello';
    final textField = ErmineTextField(
      controller: controller,
      labelText: label,
    );
    await tester.pumpWidget(_buildTextField(textField));

    expect(find.text(label), findsOneWidget);
  });

  testWidgets(
      'Should have a hint text when one is given and there is no input.',
      (WidgetTester tester) async {
    const hint = 'Type something...';
    final textField = ErmineTextField(
      controller: controller,
      hintText: hint,
    );
    await tester.pumpWidget(_buildTextField(textField));

    expect(find.text(hint), findsOneWidget);
  });

  testWidgets('Should have a fixed width for the field when one is given.',
      (WidgetTester tester) async {
    const fieldWidth = 300.0;
    final textField = ErmineTextField(
      controller: controller,
      labelText: 'Label',
      width: fieldWidth,
    );
    await tester.pumpWidget(_buildTextField(textField));
    final fieldSize = tester.getSize(find.byType(TextField));

    expect(fieldSize.width, equals(fieldWidth));
  });

  testWidgets('Should expand to the width of the parent when none is given.',
      (WidgetTester tester) async {
    final textField = ErmineTextField(
      controller: controller,
    );
    await tester.pumpWidget(_buildTextField(textField));
    final fieldSize = tester.getSize(find.byType(TextField));

    expect(fieldSize.width, equals(parentWidth));
  });
}

Widget _buildTextField(ErmineTextField textField) => MaterialApp(
      home: Scaffold(
        body: Container(
          width: parentWidth,
          child: textField,
        ),
      ),
    );
