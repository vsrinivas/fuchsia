// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../../models/ask_model.dart';
import '../../utils/elevations.dart';
import '../../utils/styles.dart';
import 'ask_suggestion_list.dart';
import 'ask_text_field.dart';

/// Defines a widget that builds the Ask bar per Ermine UX.
class Ask extends StatelessWidget {
  final AskModel model;

  const Ask({@required this.model});

  @override
  Widget build(BuildContext context) {
    return Material(
      key: model.key,
      color: ErmineStyle.kOverlayBorderColor,
      elevation: elevations.systemOverlayElevation,
      child: Column(
        children: <Widget>[
          // Text field.
          AskTextField(model: model),

          // Suggestions list.
          AskSuggestionList(model: model),
        ],
      ),
    );
  }
}
