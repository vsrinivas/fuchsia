// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:mockito/mockito.dart';

// ignore_for_file: implementation_imports
import 'package:ermine/src/models/app_model.dart';
import 'package:ermine/src/models/oobe_model.dart';

void main() {
  OobeModel oobeModel;
  final appModel = MockAppModel();

  setUp(() {
    oobeModel = OobeModel(onFinished: appModel.exitOobe);
  });

  test('OobeModel should be able to navigate between screens.', () {
    // The model should be able to navigate to the next screen.
    oobeModel.onNext();
    expect(oobeModel.currentItem.value, 1);

    // The model should be able to navigate to the previous screen.
    oobeModel.onBack();
    expect(oobeModel.currentItem.value, 0);

    // The model should not try to go back from the first screen.
    oobeModel.onBack();
    expect(oobeModel.currentItem.value, 0);

    // The model should exit after last screen.
    oobeModel.currentItem.value = oobeModel.oobeItems.length - 1;
    oobeModel.onNext();
    verify(appModel.exitOobe()).called(1);
  });
}

class MockAppModel extends Mock implements AppModel {}
