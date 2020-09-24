// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:tiler/tiler.dart' show Tiler, TileModel;
import 'package:fuchsia_scenic_flutter/child_view.dart' show ChildView;

import '../../models/cluster_model.dart';
import '../../models/ermine_story.dart';
import 'post_render.dart';
import 'tile_chrome.dart';
import 'tile_sizer.dart';
import 'tile_tab.dart';

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
    return AnimatedBuilder(
      animation: Listenable.merge([
        story.childViewConnectionNotifier,
        story.fullscreenNotifier,
      ]),
      builder: (context, child) {
        return story.childViewConnection == null || story.isImmersive
            ? Offstage()
            : AnimatedBuilder(
                animation: Listenable.merge([
                  story.nameNotifier,
                  story.focusedNotifier,
                ]),
                builder: (context, child) => TileChrome(
                  name: story.name,
                  showTitle: !custom,
                  focused: story.focused,
                  //TODO(47796) show a placeholder until the view loads
                  child: PostRender(
                    child: ChildView(connection: story.childViewConnection),
                    onRender: story.requestFocus,
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
