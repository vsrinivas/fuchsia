// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'ask_model.dart';

class AskTextField extends StatelessWidget {
  final AskModel model;

  const AskTextField({this.model});

  @override
  Widget build(BuildContext context) {
    return Material(
      color: Colors.white,
      elevation: model.elevation,
      child: Padding(
        padding: const EdgeInsets.all(16.0),
        child: TextField(
          controller: model.controller,
          decoration: InputDecoration(
            fillColor: Colors.black,
            filled: true,
            contentPadding: EdgeInsets.symmetric(
              horizontal: 8,
              vertical: 16,
            ),
            border: InputBorder.none,
            hintText: '#Ask for anything',
            hintStyle: Theme.of(context).textTheme.body1.copyWith(
                  color: Color(0xFF9AA0A6),
                  fontFamily: 'RobotoMono',
                  fontSize: 18.0,
                ),
          ),
          cursorColor: Color(0xFFFF8BCB),
          cursorRadius: Radius.zero,
          cursorWidth: 10.0,
          style: Theme.of(context).textTheme.subhead.merge(
                TextStyle(
                  color: Colors.white,
                  fontFamily: 'RobotoMono',
                  fontSize: 18.0,
                ),
              ),
          focusNode: model.focusNode,
          onChanged: model.onQuery,
          onSubmitted: model.onAsk,
        ),
      ),
    );
  }
}
