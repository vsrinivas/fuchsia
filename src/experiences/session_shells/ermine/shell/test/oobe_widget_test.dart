// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports
import 'package:ermine/src/models/app_model.dart';
import 'package:ermine/src/models/oobe_model.dart';
import 'package:ermine/src/widgets/support/oobe.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';

void main() async {
  TestOobe oobe;
  MockAppModel appModel;

  setUp(() {
    appModel = MockAppModel();
    oobe = TestOobe(OobeModel(onFinished: appModel.exitOobe));
  });

  testWidgets('Should display header, body and footer', (tester) async {
    await tester.pumpWidget(
        Directionality(textDirection: TextDirection.ltr, child: oobe));

    expect(find.byWidget(oobe.header), findsOneWidget);
    expect(find.byWidget(oobe.body), findsOneWidget);
    expect(find.byWidget(oobe.footer), findsOneWidget);
  });
}

class TestOobe extends Oobe {
  final Widget header = Container();
  final Widget body = Container();
  final Widget footer = Container();

  TestOobe(OobeModel model) : super(model: model);

  @override
  Widget buildHeader() => header;

  @override
  Widget buildBody() => body;

  @override
  Widget buildFooter() => footer;
}

class MockAppModel extends Mock implements AppModel {}
