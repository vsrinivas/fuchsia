// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
// Includes the strings used for the button labels.  Please only use the labels
// defined there, and add new ones if you need them.
import 'package:internationalization/strings.dart';

import '../../models/ask_model.dart';
import '../../utils/styles.dart';

/// Defines a [TextField] built to Ermine UX spec.
class AskTextField extends StatelessWidget {
  /// The model that holds the state for this widget.
  final AskModel model;

  /// Constructor.
  const AskTextField({@required this.model});

  @override
  Widget build(BuildContext context) {
    WidgetsBinding.instance
        .addPostFrameCallback((_) => model.focusNode.requestFocus());
    return Stack(
      children: <Widget>[
        TextField(
          controller: model.controller,
          decoration: InputDecoration(
            fillColor: Colors.white,
            filled: true,
            focusedBorder: OutlineInputBorder(
              borderRadius: BorderRadius.zero,
              borderSide: BorderSide(color: ErmineStyle.kOverlayBorderColor),
            ),
            focusColor: ErmineStyle.kOverlayBorderColor,
            border: OutlineInputBorder(
              borderRadius: BorderRadius.zero,
              borderSide: BorderSide(color: ErmineStyle.kOverlayBorderColor),
            ),
            contentPadding: EdgeInsets.symmetric(vertical: 16, horizontal: 12),
          ),
          cursorColor: Colors.black,
          cursorRadius: Radius.zero,
          cursorWidth: 10.0,
          enableInteractiveSelection: true,
          style: TextStyle(
            color: Colors.black,
            fontFamily: 'RobotoMono',
            fontSize: 18,
          ),
          focusNode: model.focusNode,
          onChanged: model.query,
          onSubmitted: model.submit,
        ),

        // Hint text.
        AnimatedBuilder(
            animation: model.controller,
            child: Padding(
              padding: EdgeInsets.only(right: 20),
              child: Text(
                Strings.typeToAsk.toUpperCase(),
                style: TextStyle(
                  color: Colors.white,
                  backgroundColor: Colors.black,
                  fontFamily: 'RobotoMono',
                  fontSize: 18,
                ),
              ),
            ),
            builder: (context, child) {
              return model.controller.text.isEmpty ? child : Offstage();
            })
      ],
      alignment: Alignment.centerRight,
    );
  }
}
