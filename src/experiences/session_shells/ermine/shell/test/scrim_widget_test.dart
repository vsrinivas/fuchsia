// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';

// ignore_for_file: implementation_imports
import 'package:ermine/src/models/app_model.dart';
import 'package:ermine/src/utils/styles.dart';
import 'package:ermine/src/widgets/support/scrim.dart';

void main() async {
  Scrim scrim;
  MockAppModel model;

  setUp(() {
    model = MockAppModel();
    scrim = Scrim(model: model);
  });

  testWidgets('Should call model.onCancel on tap', (tester) async {
    await tester.pumpWidget(scrim);
    await tester.tap(find.byWidget(scrim));

    verify(model.onCancel()).called(1);
  });

  testWidgets('Should update peekNotifier when fullscreen', (tester) async {
    final peekNotifier = ValueNotifier<bool>(false);
    when(model.peekNotifier).thenReturn(peekNotifier);
    when(model.isFullscreen).thenReturn(true);

    final TestGesture gesture =
        await tester.createGesture(kind: PointerDeviceKind.mouse);
    await gesture.addPointer(location: const Offset(100, 100));
    addTearDown(gesture.removePointer);
    await tester.pumpWidget(scrim);

    await gesture.moveTo(Offset(100, 0));
    expect(peekNotifier.value, true);

    await gesture.moveTo(Offset(
        100, ErmineStyle.kTopBarHeight + ErmineStyle.kStoryTitleHeight + 1));
    expect(peekNotifier.value, false);
  });
}

class MockAppModel extends Mock implements AppModel {}
