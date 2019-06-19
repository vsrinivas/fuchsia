// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../utils/elevations.dart';

/// Defines a widget that builds the tile chrome for a story.
class TileChrome extends StatelessWidget {
  static const _kBorderSize = 20.0;

  final bool focused;
  final bool editing;
  final bool showTitle;
  final bool fullscreen;
  final bool draggable;
  final String name;
  final Widget child;
  final double width;
  final double height;
  final ValueChanged<Offset> onDragComplete;
  final VoidCallback onDelete;
  final VoidCallback onFullscreen;
  final VoidCallback onMinimize;
  final VoidCallback onTap;
  final VoidCallback onEdit;
  final VoidCallback onCancelEdit;
  final VoidCallback onConfirmEdit;

  const TileChrome({
    @required this.name,
    this.child,
    this.editing = false,
    this.showTitle = true,
    this.fullscreen = false,
    this.focused = false,
    this.draggable = false,
    this.width,
    this.height,
    this.onDragComplete,
    this.onDelete,
    this.onFullscreen,
    this.onMinimize,
    this.onTap,
    this.onEdit,
    this.onCancelEdit,
    this.onConfirmEdit,
  });

  @override
  Widget build(BuildContext context) {
    Widget chrome = GestureDetector(
      behavior: HitTestBehavior.translucent,
      // Disable listview scrolling on top of story.
      onHorizontalDragStart: (_) {},
      onTap: onTap,
      child: Stack(
        children: [
          // Border.
          Positioned.fill(
            child: Container(
              decoration: showTitle && !fullscreen
                  ? BoxDecoration(
                      border: Border.all(
                        color: focused ? Colors.white : Colors.grey,
                        width: _kBorderSize,
                      ),
                    )
                  : null,
              child: child ?? Container(color: Colors.transparent),
            ),
          ),

          // Title.
          Positioned(
            left: 0,
            top: 0,
            right: 0,
            height: _kBorderSize,
            child: showTitle
                ? fullscreen // Display title bar on top of story.
                    ? Material(
                        elevation: elevations.systemOverlayElevation,
                        color: focused ? Colors.white : Colors.grey,
                        child: _buildTitlebar(context),
                      )
                    : _buildTitlebar(context)
                : Offstage(),
          )
        ],
      ),
    );
    return draggable
        ? Draggable(
            child: chrome,
            feedback: SizedBox(
              width: width,
              height: height,
              child: chrome,
            ),
            childWhenDragging: Container(),
            onDragCompleted: () => print('onDragCompleted'),
            onDraggableCanceled: (velocity, offset) =>
                onDragComplete?.call(offset),
          )
        : chrome;
  }

  Widget _buildTitlebar(BuildContext context) => Row(
        crossAxisAlignment: CrossAxisAlignment.center,
        children: <Widget>[
          Padding(
            padding: EdgeInsets.only(left: 8),
          ),

          // Cancel edit button.
          if (editing)
            _buildTitleBarTextButton(context, 'Cancel', () {
              onEdit?.call();
              onCancelEdit?.call();
            }),

          // Story name.
          Expanded(
            child: _buildTitleBarTextButton(context, name ?? '<>', onEdit),
          ),

          // Minimize button.
          if (!editing)
            _buildIconButton(context, Icons.remove, onMinimize),

          if (!editing)
            Padding(
              padding: EdgeInsets.only(left: 8),
            ),

          // Maximize button.
          if (!editing)
            _buildIconButton(context, Icons.add, onFullscreen),

          if (!editing)
            Padding(
              padding: EdgeInsets.only(left: 8),
            ),

          // Close button.
          if (!editing)
            _buildIconButton(context, Icons.clear, onDelete),

          // Done edit button.
          if (editing)
            _buildTitleBarTextButton(context, 'Done', () {
              onEdit?.call();
              onConfirmEdit?.call();
            }),

          Padding(
            padding: EdgeInsets.only(left: 8),
          ),
        ],
      );

  Widget _buildTitleBarTextButton(
    BuildContext context,
    String title,
    VoidCallback onTap,
  ) =>
      GestureDetector(
        onTap: onTap,
        child: Center(
          child: Text(
            title,
            textAlign: TextAlign.center,
            style: Theme.of(context)
                .textTheme
                .caption
                .copyWith(color: focused ? Colors.black : Colors.white),
          ),
        ),
      );

  Widget _buildIconButton(
    BuildContext context,
    IconData icon,
    VoidCallback onTap,
  ) =>
      GestureDetector(
        child: Icon(
          icon,
          size: _kBorderSize,
          color: focused ? Colors.black : Colors.white,
        ),
        onTap: onTap?.call,
      );
}
