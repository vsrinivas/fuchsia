// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_ui/ermine_ui.dart';
import 'package:flutter/material.dart';
import 'package:internationalization/strings.dart';

import '../../models/oobe_model.dart';
import '../../utils/styles.dart';
import 'oobe_buttons.dart';
import 'oobe_header.dart';

class Channel extends StatelessWidget {
  final OobeModel model;

  const Channel({@required this.model});

  @override
  Widget build(BuildContext context) {
    return Column(children: <Widget>[
      OobeHeader(Strings.oobeChannelTitle,
          [DescriptionModel(text: Strings.oobeChannelDesc)]),
      // Body.
      Expanded(
        child: Container(
          margin: EdgeInsets.symmetric(
              vertical: ErmineStyle.kOobeBodyVerticalMargins),
          child: Text(
            'Channel selection placeholder',
            textAlign: TextAlign.center,
            style: ErmineTextStyles.headline4,
          ),
        ),
      ),
      OobeButtons([
        OobeButtonModel(Strings.back, model.onBack),
        OobeButtonModel(Strings.skip, model.onNext),
      ]),
    ]);
  }
}
