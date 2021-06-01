// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports
import 'package:ermine/src/models/app_model.dart';
import 'package:ermine/src/models/topbar_model.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

void main() {
  TopbarModel topbarModel;
  final appModel = MockAppModel();

  setUp(() {
    topbarModel = TopbarModel(appModel: appModel);
  });

  test('Topbar element visibility calls into AppModel', () {
    topbarModel.showAsk();
    verify(appModel.onAsk()).called(1);

    topbarModel.showOverview();
    verify(appModel.onOverview()).called(1);

    topbarModel.showRecents();
    verify(appModel.onRecents()).called(1);

    topbarModel.showKeyboardHelp();
    verify(appModel.onKeyboard()).called(1);

    topbarModel.showStatus();
    verify(appModel.onStatus()).called(1);
  });
}

class MockAppModel extends Mock implements AppModel {}
