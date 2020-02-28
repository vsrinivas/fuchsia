// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

// ignore_for_file: implementation_imports
import 'package:fidl/fidl.dart';
import 'package:ermine_library/src/models/ermine_story.dart';
import 'package:ermine_library/src/utils/suggestion.dart';
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

void main() {
  MockComponentController controller;
  MockProxyController proxy;
  StreamController<ComponentController$OnTerminated$Response>
      onTerminatedEventStreamController;

  setUp(() async {
    controller = MockComponentController();
    proxy = MockProxyController();
    when(controller.ctrl).thenReturn(proxy);

    onTerminatedEventStreamController =
        StreamController<ComponentController$OnTerminated$Response>.broadcast(
            sync: true);
    when(controller.onTerminated)
        .thenAnswer((_) => onTerminatedEventStreamController.stream);
  });

  tearDown(() async {
    await onTerminatedEventStreamController.close();
  });

  test('Delete ErmineStory', () {
    // Creating a story should also launch it.
    final suggestion = Suggestion(id: 'id', title: 'title', url: 'url');
    final story = TestErmineStory(
      suggestion: suggestion,
      launcher: MockLauncher(),
      componentController: controller,
    );
    expect(story.launched, true);

    // Delete should call deleted.
    story.delete();
    expect(story.deleted, true);
    verify(controller.kill()).called(1);
    verify(proxy.close()).called(1);
  });

  test('Terminate ErmineStory', () {
    // Creating a story should also launch it.
    final suggestion = Suggestion(id: 'id', title: 'title', url: 'url');
    final story = TestErmineStory(
      suggestion: suggestion,
      launcher: MockLauncher(),
      componentController: controller,
    );
    expect(story.launched, true);

    // Terminated should call deleted.
    final response =
        ComponentController$OnTerminated$Response(0, TerminationReason.exited);
    onTerminatedEventStreamController.add(response);
    expect(story.deleted, true);
    verify(controller.kill()).called(1);
    verify(proxy.close()).called(1);
  });
}

class TestErmineStory extends ErmineStory {
  bool deleted;
  bool changed;
  bool launched;
  MockLauncher launcher;
  MockComponentController componentController;

  TestErmineStory({
    Suggestion suggestion,
    this.launcher,
    this.componentController,
  }) : super(
          suggestion: suggestion,
          launcher: launcher,
          componentController: componentController,
          onDelete: _onDelete,
          onChange: _onChange,
        );

  static void _onDelete(ErmineStory story) {
    if (story is TestErmineStory) {
      story.deleted = true;
    }
  }

  static void _onChange(ErmineStory story) {
    if (story is TestErmineStory) {
      story.changed = true;
    }
  }

  @override
  Future<void> launchSuggestion() async => launched = true;
}

class MockLauncher extends Mock implements LauncherProxy {}

class MockComponentController extends Mock implements ComponentControllerProxy {
}

class MockProxyController extends Mock
    implements AsyncProxyController<ComponentController> {}
