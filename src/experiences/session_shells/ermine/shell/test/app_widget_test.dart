// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:intl/intl.dart';
import 'package:mockito/mockito.dart';

// ignore_for_file: implementation_imports
import 'package:ermine_library/src/models/app_model.dart';
import 'package:ermine_library/src/widgets/app.dart';

void main() async {
  setupLogger(name: 'ermine_unittests');

  testWidgets('Test locale change', (tester) async {
    final swissFrench = Locale('fr', 'CH');
    final model = MockAppModel();
    when(model.localeStream)
        .thenAnswer((_) => Stream<Locale>.value(swissFrench));
    when(model.overviewVisibility).thenReturn(ValueNotifier<bool>(false));

    final app = App(model: model);

    await tester.pumpWidget(app);
    expect(Intl.defaultLocale, null);

    await tester.pumpWidget(app);
    expect(Intl.defaultLocale, swissFrench.toString());
  });
}

class MockAppModel extends Mock implements AppModel {}
