// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'package:fuchsia_inspect/inspect.dart';

import '../../models/ask_model.dart';
import '../../utils/styles.dart';
import '../../utils/suggestions.dart';
import '../../utils/utils.dart';
import 'ask_suggestion_list.dart';
import 'ask_text_field.dart';

/// Defines a widget that builds the Ask bar per Ermine UX.
class Ask extends StatefulWidget {
  final SuggestionService suggestionService;
  final VoidCallback onDismiss;
  final bool unbounded;

  const Ask({
    @required this.suggestionService,
    Key key,
    this.onDismiss,
    this.unbounded = false,
  }) : super(key: key);

  @override
  AskState createState() => AskState();
}

class AskState extends State<Ask> implements Inspectable {
  AskModel model;

  @override
  void initState() {
    super.initState();

    model = AskModel(
      suggestionService: widget.suggestionService,
      onDismiss: widget.onDismiss,
    )..query('');
  }

  @override
  void dispose() {
    model.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      color: ErmineStyle.kBackgroundColor,
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: <Widget>[
          // Text field.
          AskTextField(model: model),

          // Suggestions list.
          Flexible(
            fit: FlexFit.loose,
            child: AskSuggestionList(model: model, unbounded: widget.unbounded),
          ),
        ],
      ),
    );
  }

  @override
  void onInspect(Node node) {
    final rect = rectFromGlobalKey(widget.key);
    if (rect == null) {
      return;
    }
    node
        .stringProperty('rect')
        .setValue('${rect.left},${rect.top},${rect.width},${rect.height}');
  }
}
