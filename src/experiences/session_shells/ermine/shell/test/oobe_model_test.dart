// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports
import 'package:ermine/src/models/app_model.dart';
import 'package:ermine/src/models/oobe_model.dart';
import 'package:flutter/material.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

void main() {
  TestOobeModel oobeModel;
  final appModel = MockAppModel();

  setUp(() {
    oobeModel = TestOobeModel(appModel.exitOobe)..loadTestOobeItems();
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

class TestOobeModel extends OobeModel {
  TestOobeModel(VoidCallback onFinished) : super(onFinished: onFinished);

  // The real OOBE items cannot be loaded in a test environment so instead we
  // use this. For the purpose of the test we do not care what the items are
  // as long as there are multiple items.
  void loadTestOobeItems() {
    oobeItems = [
      Container(), // In place of Channel
      Container(), // In place of DataSharing
      Container(), // In place of SshKeys
    ];
  }
}
