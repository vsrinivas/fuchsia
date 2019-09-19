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
              padding: showTitle
                  ? EdgeInsets.only(
                      top: ErmineStyle.kStoryTitleHeight,
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

          // Title.
          Positioned(
            left: 0,
            top: 0,
            right: 0,
            height: ErmineStyle.kStoryTitleHeight,
            child: showTitle ? _buildTitlebar(context) : Offstage(),
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
          if (editing)
            Padding(
              padding: EdgeInsets.only(left: 8),
            ),

          // Minimize button.
          if (!editing && focused)
            _buildIconButton(maximize: false, onTap: onDelete),

          // Cancel edit button.
          if (editing)
            _buildTitleBarTextButton(context, 'Cancel', onTapCancel),

          // Story name.
          Expanded(
            child: editing
                ? TextField(
                    controller: titleFieldController,
                    autofocus: true,
                    onSubmitted: (_) => onTapDone(),
                    cursorWidth: 8,
                    cursorRadius: Radius.zero,
                    cursorColor: Colors.white,
                    textAlign: TextAlign.center,
                    decoration: InputDecoration(
                      contentPadding: EdgeInsets.zero,
                      hintText: '              NAME THIS STORY',
                      border: InputBorder.none,
                      isDense: true,
                    ),
                  )
                : _buildTitleBarTextButton(context, name ?? '<>', onEdit),
          ),

          // Maximize button.
          if (!editing && focused)
            _buildIconButton(
              maximize: true,
              onTap: fullscreen ? onMinimize : onFullscreen,
            ),

          // Done edit button.
          if (editing)
            _buildTitleBarTextButton(context, 'Done', onTapDone),

          if (editing)
            Padding(
              padding: EdgeInsets.only(left: 8),
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
