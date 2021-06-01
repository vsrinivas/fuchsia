// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports
import 'package:ermine/src/models/app_model.dart';
import 'package:ermine/src/widgets/support/recents.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';

void main() async {
  TestRecentsContainer recents;
  MockAppModel model;

  setUp(() {
    model = MockAppModel();
    recents = TestRecentsContainer(model);
  });

  testWidgets('Test Recents visibility', (tester) async {
    final recentsVisibility = ValueNotifier<bool>(true);
    when(model.recentsVisibility).thenReturn(recentsVisibility);
    await tester.pumpWidget(recents);
    expect(find.byWidget(recents.thumbnails), findsOneWidget);

    recentsVisibility.value = false;
    await tester.pumpAndSettle();
    expect(find.byWidget(recents.thumbnails), findsNothing);
  });
}

class TestRecentsContainer extends RecentsContainer {
  final Widget thumbnails = Container();

  TestRecentsContainer(AppModel model) : super(model: model);

  @override
  Widget buildThumbnails(AppModel model) => thumbnails;
}

class MockAppModel extends Mock implements AppModel {}
