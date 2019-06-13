// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../models/app_model.dart';
import 'story_widget.dart';
import 'tile_chrome.dart';

/// Defines a widget to display a story fullscreen.
class FullscreenStory extends StatelessWidget {
  final AppModel model;

  const FullscreenStory(this.model);

  @override
  Widget build(BuildContext context) {
    final showFullscreenTitle = ValueNotifier<bool>(false);

    return AnimatedBuilder(
      animation: model.clustersModel.fullscreenStoryNotifier,
      builder: (context, child) {
        final story = model.clustersModel.fullscreenStory;
        final confirmEditNotifier = ValueNotifier<bool>(null);
        return story != null
            ? AnimatedBuilder(
                animation: Listenable.merge([
                  showFullscreenTitle,
                  story.editStateNotifier,
                ]),
                builder: (context, _) {
                  return Listener(
                    onPointerHover: (event) {
                      if (event.position.dy == 0) {
                        showFullscreenTitle.value = true;
                      } else if (event.position.dy > 120) {
                        showFullscreenTitle.value = false;
                      }
                    },
                    child: TileChrome(
                      name: story.id,
                      focused: story.focused,
                      showTitle: showFullscreenTitle.value,
                      editing: story.editStateNotifier.value,
                      fullscreen: true,
                      child: StoryWidget(
                        editing: story.editStateNotifier.value,
                        presenter: story.layoutManager.presenter,
                        confirmEdit: confirmEditNotifier,
                      ),
                      onDelete: story.delete,
                      onMinimize: story.restore,
                      onEdit: story.edit,
                      onCancelEdit: () => confirmEditNotifier.value = false,
                      onConfirmEdit: () => confirmEditNotifier.value = true,
                    ),
                  );
                })
            : Offstage();
      },
    );
  }
}
