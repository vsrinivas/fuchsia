// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'ask_model.dart';

const _kTextColor = Colors.white;
const _kTextColorHighlight = Color(0xFFFF8BCA);

class AskSuggestionList extends StatelessWidget {
  static const double kListItemHeight = 56.0;

  final AskModel model;

  const AskSuggestionList({this.model});

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: model.suggestions,
      builder: (context, child) => RawKeyboardListener(
            onKey: model.onKey,
            focusNode: model.focusNode,
            child: SliverFixedExtentList(
              itemExtent: kListItemHeight,
              delegate: SliverChildBuilderDelegate(
                _buildItem,
                childCount: model.suggestions.value.length,
              ),
            ),
          ),
    );
  }

  Widget _buildItem(context, index) {
    final suggestion = model.suggestions.value[index];
    return Listener(
      onPointerEnter: (_) {
        model.selection.value = index;
      },
      onPointerExit: (_) {
        if (model.selection.value == index) {
          model.selection.value = -1;
        }
      },
      child: GestureDetector(
        onTap: () => model.onSelect(suggestion),
        child: Container(
          alignment: Alignment.centerLeft,
          height: kListItemHeight,
          padding: EdgeInsets.symmetric(horizontal: 22.0),
          child: AnimatedBuilder(
            animation: model.selection,
            builder: (context, child) {
              return Text(
                suggestion.displayInfo.title,
                maxLines: 1,
                softWrap: false,
                overflow: TextOverflow.fade,
                textAlign: TextAlign.start,
                style: TextStyle(
                  color: model.selection.value == index
                      ? _kTextColorHighlight
                      : _kTextColor,
                  fontFamily: 'RobotoMono',
                  fontSize: 18.0,
                ),
              );
            },
          ),
        ),
      ),
    );
  }
}
