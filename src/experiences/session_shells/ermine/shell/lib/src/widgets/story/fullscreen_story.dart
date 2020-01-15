// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_scenic_flutter/child_view.dart' show ChildView;

import '../../models/app_model.dart';
import '../../utils/styles.dart';
import 'tile_chrome.dart';

/// Defines a widget to display a story fullscreen.
class FullscreenStory extends StatelessWidget {
  final AppModel model;

  const FullscreenStory(this.model);

  @override
  Widget build(BuildContext context) {
    return Positioned(
      top: 0,
      left: 0,
      right: 0,
      bottom: -ErmineStyle.kTopBarHeight - ErmineStyle.kStoryTitleHeight,
      child: AnimatedBuilder(
        animation: model.clustersModel.fullscreenStoryNotifier,
        builder: (context, child) {
          final story = model.clustersModel.fullscreenStory;
          final confirmEditNotifier = ValueNotifier<bool>(null);
          final titleFieldController = TextEditingController(text: '');
          return story != null
              ? AnimatedBuilder(
                  animation: Listenable.merge([
                    story.nameNotifier,
                    story.editStateNotifier,
                  ]),
                  builder: (context, _) {
                    return TileChrome(
                      name: story.name,
                      focused: story.focused,
                      fullscreen: true,
                      editing: story.editStateNotifier.value,
                      titleFieldController: titleFieldController,
                      child: ChildView(connection: story.childViewConnection),
                      onDelete: story.delete,
                      onMinimize: story.restore,
                      onFullscreen: story.maximize,
                      onEdit: story.edit,
                      onCancelEdit: () => confirmEditNotifier.value = false,
                      onConfirmEdit: () => confirmEditNotifier.value = true,
                    );
                  })
              : Offstage();
        },
      ),
    );
  }
}
