// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../../utils/styles.dart';

/// Defines a widget that builds the tile chrome for a story.
class TileChrome extends StatelessWidget {
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
  final TextEditingController titleFieldController;

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
    this.titleFieldController,
  });

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      behavior: HitTestBehavior.translucent,
      // Disable listview scrolling on top of story.
      onHorizontalDragStart: (_) {},
      onTap: onTap,
      child: Stack(
        children: [
          // Title.
          Positioned(
            left: 0,
            top: 0,
            right: 0,
            height: ErmineStyle.kStoryTitleHeight,
            child: showTitle ? _buildTitlebar(context) : Offstage(),
          ),

          // Border.
          Positioned(
            left: 0,
            top: fullscreen ? 0 : ErmineStyle.kStoryTitleHeight,
            right: 0,
            bottom: 0,
            child: Container(
              padding: showTitle
                  ? EdgeInsets.only(
                      top: fullscreen ? ErmineStyle.kStoryTitleHeight : 0,
                      left: ErmineStyle.kBorderWidth,
                      right: ErmineStyle.kBorderWidth,
                      bottom: ErmineStyle.kBorderWidth,
                    )
                  : null,
              color: ErmineStyle.kStoryTitleBackgroundColor,
              child: ClipRect(
                child: child ?? Container(color: Colors.transparent),
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildTitlebar(BuildContext context) => Row(
        crossAxisAlignment: CrossAxisAlignment.center,
        children: <Widget>[
          // Minimize button.
          if (focused) _buildIconButton(maximize: false, onTap: onDelete),

          // Story name.
          Expanded(
            child: _buildTitleBarTextButton(context, name ?? '<>', onEdit),
          ),

          // Maximize button.
          if (focused)
            _buildIconButton(
              maximize: true,
              onTap: fullscreen ? onMinimize : onFullscreen,
            ),
        ],
      );

  void onTapCancel() {
    onEdit?.call();
    onCancelEdit?.call();
  }

  void onTapDone() {
    onEdit?.call();
    onConfirmEdit?.call();
  }

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
                .copyWith(color: ErmineStyle.kStoryTitleColor),
          ),
        ),
      );

  Widget _buildIconButton({
    bool maximize,
    VoidCallback onTap,
  }) =>
      GestureDetector(
        child: Container(
          width: ErmineStyle.kStoryTitleHeight,
          height: ErmineStyle.kStoryTitleHeight,
          color: ErmineStyle.kStoryTitleBackgroundColor,
          alignment: maximize ? Alignment.centerRight : Alignment.centerLeft,
          child: Container(
            width: 10,
            height: 10,
            decoration: BoxDecoration(
              color: ErmineStyle.kStoryTitleColor,
              shape: maximize ? BoxShape.rectangle : BoxShape.circle,
            ),
          ),
        ),
        onTap: onTap?.call,
      );
}
