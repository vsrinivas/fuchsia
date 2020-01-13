// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../../models/ask_model.dart';
import '../../utils/styles.dart';

final _kTextBackground = Colors.grey[800];

/// Defines a widget that builds the list of suggestions under the [Ask] widget.
class AskSuggestionList extends StatelessWidget {
  static const double _kListItemHeight = 56.0;
  static const int _kListItemCount = 5;
  static const double _kListViewHeight = _kListItemHeight * _kListItemCount;

  /// The model that holds the state for this widget.
  final AskModel model;
  final bool unbounded;

  const AskSuggestionList({@required this.model, this.unbounded = false});

  @override
  Widget build(BuildContext context) {
    final controller = ScrollController();
    model.selection.addListener(() {
      int index = model.selection.value;
      if (controller.hasClients) {
        final itemOffset = index * _kListItemHeight;
        final scrollOffset = controller.position.pixels;
        final viewport = controller.position.viewportDimension;
        bool above = itemOffset < scrollOffset;
        bool below = itemOffset + _kListItemHeight > scrollOffset + viewport;
        bool visible = !above && !below;
        if (!visible) {
          final newOffset =
              above ? itemOffset : itemOffset - viewport + _kListItemHeight;
          controller.jumpTo(newOffset);
        }
      }
    });
    return RawKeyboardListener(
      onKey: model.handleKey,
      focusNode: model.focusNode,
      child: AnimatedBuilder(
          animation: model.suggestions,
          builder: (context, child) {
            return Container(
              decoration: model.suggestions.value.isNotEmpty && !unbounded
                  ? BoxDecoration(
                      color: ErmineStyle.kOverlayBackgroundColor,
                      border: Border.all(
                        color: ErmineStyle.kOverlayBorderColor,
                        width: ErmineStyle.kOverlayBorderWidth,
                      ),
                      borderRadius: BorderRadius.all(Radius.zero),
                    )
                  : null,
              constraints: unbounded
                  ? null
                  : BoxConstraints(maxHeight: _kListViewHeight),
              child: AnimatedList(
                controller: controller,
                key: model.suggestionsListKey,
                shrinkWrap: true,
                itemBuilder: _buildItem,
              ),
            );
          }),
    );
  }

  Widget _buildItem(
    BuildContext context,
    int index,
    Animation<double> animation,
  ) {
    final suggestion = model.suggestions.value[index];
    return FadeTransition(
      opacity: animation,
      child: Listener(
        // ignore: deprecated_member_use
        onPointerHover: (_) {
          model.selection.value = index;
        },
        child: GestureDetector(
          onTap: () => model.handleSuggestion(suggestion),
          child: AnimatedBuilder(
            animation: model.selection,
            builder: (context, child) {
              return Container(
                alignment: Alignment.centerLeft,
                height: _kListItemHeight,
                padding: EdgeInsets.symmetric(horizontal: 12.0),
                color: model.selection.value == index ? _kTextBackground : null,
                child: Row(
                  children: <Widget>[
                    Expanded(
                      child: Text(
                        suggestion.title,
                        maxLines: 1,
                        softWrap: false,
                        overflow: TextOverflow.fade,
                        textAlign: TextAlign.start,
                        style: TextStyle(
                          color: Colors.white,
                          backgroundColor: model.selection.value == index
                              ? _kTextBackground
                              : Colors.black,
                          fontFamily: 'Roboto Mono',
                          fontSize: 18.0,
                        ),
                      ),
                    ),
                    Padding(padding: EdgeInsets.only(left: 12)),
                    Text(
                      'PACKAGE',
                      overflow: TextOverflow.fade,
                      maxLines: 1,
                      style: TextStyle(
                        color: Colors.black,
                        backgroundColor: Colors.white,
                        fontFamily: 'Roboto Mono',
                        fontSize: 12.0,
                      ),
                    ),
                  ],
                ),
              );
            },
          ),
        ),
      ),
    );
  }
}
