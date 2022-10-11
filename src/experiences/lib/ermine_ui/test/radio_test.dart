// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_ui/ermine_ui.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('Verify the color of active radio', (WidgetTester tester) async {
    final ermineRadio =
        ErmineRadio(value: true, groupValue: true, onChanged: (value) {});
    await tester.pumpWidget(_buildRadio(ermineRadio));

    final radio = tester.firstWidget(find.byType(Radio<bool>(
      value: true,
      groupValue: true,
      onChanged: (value) {},
      // ignore: avoid_as
    ).runtimeType)) as Radio;

    expect(radio.activeColor, ErmineColors.grey100);
  });
}

Widget _buildRadio(ErmineRadio radio) => MaterialApp(
      home: Scaffold(
        body: radio,
      ),
    );
