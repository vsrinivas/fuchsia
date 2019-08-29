// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_modular/fidl_async.dart'
    show StoryVisualStateWatcher, StoryVisualState, StoryShellContext;
import 'package:fidl_fuchsia_ui_policy/fidl_async.dart';
import 'package:lib.story_shell/common.dart';
import 'package:lib.widgets/utils.dart';

class VisualStateWatcher extends StoryVisualStateWatcher {
  StoryVisualState _visualState;
  final PointerEventsListener pointerEventsListener = PointerEventsListener();
  final KeyListener keyListener;
  final StoryShellContext storyShellContext;

  VisualStateWatcher({
    this.keyListener,
    this.storyShellContext,
  });

  @override
  Future<void> onVisualStateChange(StoryVisualState visualState) async {
    if (_visualState == visualState) {
      return;
    }
    _visualState = visualState;

    pointerEventsListener.stop();
    if (visualState == StoryVisualState.maximized) {
      PresentationProxy presentationProxy = PresentationProxy();
      await storyShellContext.getPresentation(presentationProxy.ctrl.request());
      pointerEventsListener.listen(presentationProxy);
      keyListener?.listen(presentationProxy);
      presentationProxy.ctrl.close();
    } else {
      pointerEventsListener.stop();
      keyListener?.stop();
    }
  }
}
