// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../../models/ask_model.dart';

final _kTextBackground = Colors.grey[800];

/// Defines a widget that builds the list of suggestions under the [Ask] widget.
class AskSuggestionList extends StatelessWidget {
  static const double _kListItemHeight = 56.0;
  static const double _kListViewHeight = _kListItemHeight * 5;

  /// The model that holds the state for this widget.
  final AskModel model;

  const AskSuggestionList({@required this.model});

  @override
  Widget build(BuildContext context) {
    return RawKeyboardListener(
      onKey: model.handleKey,
      focusNode: model.focusNode,
      child: Container(
        color: Color(0xFF0C0C0C),
        constraints: BoxConstraints(maxHeight: _kListViewHeight),
        child: AnimatedList(
          key: model.suggestionsListKey,
          shrinkWrap: true,
          itemBuilder: _buildItem,
        ),
      ),
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
                        suggestion.displayInfo.title,
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
