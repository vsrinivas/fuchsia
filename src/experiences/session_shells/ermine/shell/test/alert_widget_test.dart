// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports
import 'package:ermine/src/models/alert_model.dart';
import 'package:ermine/src/models/app_model.dart';
import 'package:ermine/src/widgets/support/alert.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';

void main() async {
  TestAlertContainer alert;
  MockAppModel model;
  AlertsModel alertsModel;

  setUp(() {
    model = MockAppModel();
    alert = TestAlertContainer(model);
    alertsModel = AlertsModel();
  });

  testWidgets('Test Alert visibility', (tester) async {
    final alertVisibility = ValueNotifier<bool>(true);
    when(model.alertsModel).thenReturn(alertsModel);
    when(model.alertVisibility).thenReturn(alertVisibility);

    await tester.pumpWidget(alert);
    expect(find.byWidget(alert.alertDialog), findsOneWidget);

    alertVisibility.value = false;
    alertsModel.notifyListeners();
    await tester.pumpAndSettle();
    expect(find.byWidget(alert.alertDialog), findsNothing);
  });
}

class TestAlertContainer extends AlertContainer {
  final alertDialog = Container();
  TestAlertContainer(AppModel model) : super(model: model);

  @override
  Widget buildAlertDialog(AppModel model) => alertDialog;
}

class MockAppModel extends Mock implements AppModel {}
