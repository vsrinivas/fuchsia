// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports
import 'package:ermine/src/models/app_model.dart';
import 'package:ermine/src/widgets/support/overview.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';

void main() async {
  TestOverview overview;
  MockAppModel model;

  setUp(() {
    model = MockAppModel();
    overview = TestOverview(model);
  });

  testWidgets('Should display ask, status and thumbnails', (tester) async {
    await tester.pumpWidget(
        Directionality(textDirection: TextDirection.ltr, child: overview));

    expect(find.byWidget(overview.thumbnails), findsOneWidget);
    expect(find.byWidget(overview.ask), findsOneWidget);
    expect(find.byWidget(overview.status), findsOneWidget);
  });
}

class TestOverview extends Overview {
  final Widget thumbnails = Container();
  final Widget ask = Container();
  final Widget status = Container();

  TestOverview(AppModel model) : super(model: model);

  @override
  Widget buildThumbnails(AppModel model) => thumbnails;

  @override
  Widget buildAsk(AppModel model) => ask;

  @override
  Widget buildStatus(AppModel model) => status;
}

class MockAppModel extends Mock implements AppModel {}
