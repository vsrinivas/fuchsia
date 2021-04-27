// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_ui/ermine_ui.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import '../../utils/styles.dart';

class OobeHeader extends StatelessWidget {
  final String title;
  final List<DescriptionModel> descriptions;

  const OobeHeader(this.title, this.descriptions);

  @override
  Widget build(BuildContext context) {
    return Column(children: <Widget>[
      // Title.
      Text(
        title,
        textAlign: TextAlign.center,
        style: ErmineTextStyles.headline1,
      ),
      Padding(
          padding: EdgeInsets.only(
              bottom: ErmineStyle.kOobeTitleDescriptionPadding)),
      // Description
      _buildDescription(),
    ]);
  }

  Widget _buildDescription() => Container(
      width: ErmineStyle.kOobeDescriptionWidth,
      child: RichText(
        text: TextSpan(
          children: <TextSpan>[
            for (final DescriptionModel description in descriptions)
              TextSpan(
                text: description.text,
                style: description.style,
                recognizer: TapGestureRecognizer()
                  ..onTap = description.onClicked,
              ),
          ],
        ),
        textAlign: TextAlign.center,
      ));
}

class DescriptionModel {
  final String text;
  final VoidCallback onClicked;
  final TextStyle style;

  DescriptionModel({
    @required this.text,
    this.onClicked,
    this.style = ErmineTextStyles.headline4,
  });
}
