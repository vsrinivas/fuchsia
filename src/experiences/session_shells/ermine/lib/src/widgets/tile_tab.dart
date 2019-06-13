// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:tiler/tiler.dart';

import '../models/ermine_story.dart';

const _kBorderSize = 16.0;

/// Defines a widget to allow tabbing between stories.
class TabbedTiles extends StatefulWidget {
  final List<TileModel<ErmineStory>> tiles;
  final TileChromeBuilder<ErmineStory> chromeBuilder;
  final ValueChanged<int> onSelect;
  final int initialIndex;

  const TabbedTiles({
    this.tiles,
    this.chromeBuilder,
    this.onSelect,
    this.initialIndex,
  });

  @override
  _TabbedTilesState createState() => _TabbedTilesState();
}

class _TabbedTilesState extends State<TabbedTiles>
    with TickerProviderStateMixin {
  TabController _controller;
  Listenable _focusListenables;

  @override
  void initState() {
    super.initState();
    _init();
  }

  void _init() {
    _cleanup();

    _controller = TabController(
      vsync: this,
      initialIndex: widget.initialIndex,
      length: widget.tiles.length,
    );
    // Listen for focus change on the tiles.
    _focusListenables = Listenable.merge(
        widget.tiles.map((t) => t.content.focusedNotifier).toList())
      ..addListener(_onFocusChange);
  }

  void _cleanup() {
    _focusListenables?.removeListener(_onFocusChange);
    _controller?.dispose();
  }

  @override
  void didUpdateWidget(TabbedTiles oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.tiles.length != widget.tiles.length ||
        !oldWidget.tiles.every((t) => widget.tiles.contains(t))) {
      _init();
    }
  }

  @override
  void dispose() {
    _cleanup();
    super.dispose();
  }

  void _onFocusChange() {
    final tile = widget.tiles.firstWhere(
      (t) => t.content.focused,
      orElse: () => null,
    );
    if (tile != null) {
      _controller.index = widget.tiles.indexOf(tile);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: <Widget>[
        TabBar(
          controller: _controller,
          isScrollable: true,
          indicator: BoxDecoration(
            color: Colors.white,
            borderRadius: BorderRadius.only(
              topLeft: Radius.circular(5),
              topRight: Radius.circular(5),
            ),
          ),
          labelColor: Colors.black,
          unselectedLabelColor: Colors.white,
          tabs: widget.tiles.map(_buildTab).toList(),
          onTap: widget.onSelect,
        ),
        Expanded(
          child: Container(
            decoration: BoxDecoration(
              border: Border.all(
                color: Colors.white,
                width: _kBorderSize,
              ),
            ),
            child: AnimatedBuilder(
              animation: _focusListenables,
              builder: (context, child) => widget.chromeBuilder(
                  context, widget.tiles[_controller.index]),
            ),
          ),
        ),
      ],
    );
  }

  Widget _buildTab(TileModel<ErmineStory> tile) {
    return Tab(
      child: Row(
        children: <Widget>[
          Text(tile.content.id),
          Padding(
            padding: EdgeInsets.only(left: 8),
          ),
          GestureDetector(
            child: Icon(
              Icons.clear,
              size: 16,
              // color: Colors.black,
            ),
            onTap: tile.content.delete,
          ),
        ],
      ),
    );
  }
}
