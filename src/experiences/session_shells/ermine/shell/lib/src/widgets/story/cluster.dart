// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:ermine_ui/ermine_ui.dart' as ermine_ui;
import 'package:flutter/material.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart' show FuchsiaView;
import 'package:tiler/tiler.dart' show Tiler, TileModel;

import '../../models/cluster_model.dart';
import '../../models/ermine_story.dart';
import 'tile_chrome.dart';
import 'tile_sizer.dart';
import 'tile_tab.dart';

const _kDelay = 500;

/// Defines a widget to cluster [Story] widgets built by the [Tiler] widget.
class Cluster extends StatelessWidget {
  final ClusterModel model;

  const Cluster({this.model});

  @override
  Widget build(BuildContext context) {
    return Tiler<ErmineStory>(
      model: model.tilerModel,
      chromeBuilder: _chromeBuilder,
      customTilesBuilder: (context, tiles) => TabbedTiles(
        tiles: tiles,
        chromeBuilder: (context, tile) => _chromeBuilder(
          context,
          tile,
          custom: true,
        ),
        onSelect: (index) => tiles[index].content.focus(),
        initialIndex: _focusedIndex(tiles),
      ),
      sizerBuilder: (context, direction, _, __) => GestureDetector(
        // Disable listview scrolling on top of sizer.
        onHorizontalDragStart: (_) {},
        behavior: HitTestBehavior.translucent,
        child: TileSizer(
          direction: direction,
        ),
      ),
      sizerThickness: TileSizer.kThickness,
    );
  }

  Widget _chromeBuilder(BuildContext context, TileModel<ErmineStory> tile,
      {bool custom = false}) {
    final story = tile.content;
    final isDelayOver = ValueNotifier(false);

    return AnimatedBuilder(
      animation: Listenable.merge([
        story.fuchsiaViewConnectionNotifier,
        story.fullscreenNotifier,
      ]),
      builder: (context, child) {
        return story.fuchsiaViewConnection == null || story.isImmersive
            ? Offstage()
            : AnimatedBuilder(
                animation: Listenable.merge([
                  story.nameNotifier,
                  story.focusedNotifier,
                  story.hittestableNotifier,
                ]),
                builder: (context, child) => TileChrome(
                  name: story.name,
                  showTitle: !custom,
                  focused: story.focused,
                  child: Stack(
                    alignment: Alignment.center,
                    children: [
                      FuchsiaView(
                        controller: story.fuchsiaViewConnection,
                        hitTestable: story.hittestable,
                      ),
                      AnimatedBuilder(
                          animation: Listenable.merge([
                            story.childViewAvailableNotifier,
                            isDelayOver,
                          ]),
                          // Displays a loading indicator if the child view
                          // is not available within [_kDelay] after launching
                          // the element.
                          builder: (context, child) {
                            final isAvailable =
                                story.childViewAvailableNotifier.value;
                            final showPlaceHolder = isDelayOver.value;
                            if (!isAvailable && showPlaceHolder) {
                              return Container(
                                alignment: Alignment.center,
                                child: ermine_ui.LoadingIndicator(
                                  description: 'Launching ${story.name}...',
                                ),
                              );
                            } else if (!isAvailable && !showPlaceHolder) {
                              Timer(Duration(milliseconds: _kDelay), () {
                                isDelayOver.value = true;
                              });
                            } else if (isAvailable) {
                              isDelayOver.value = false;
                            }
                            return Offstage();
                          }),
                    ],
                  ),
                  onTap: story.focus,
                  onDelete: story.delete,
                  onFullscreen: story.maximize,
                  onMinimize: story.restore,
                ),
              );
      },
    );
  }

  int _focusedIndex(List<TileModel<ErmineStory>> tiles) {
    final focusedTile = tiles.firstWhere(
      (t) => t.content.focused,
      orElse: () => null,
    );
    return focusedTile == null ? 0 : tiles.indexOf(focusedTile);
  }
}
