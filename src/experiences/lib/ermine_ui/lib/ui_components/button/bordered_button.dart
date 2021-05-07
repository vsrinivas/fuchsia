// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'button.dart';
import 'layout.dart';

/// Button with borders and no background color.
class BorderedButton extends ErmineButton {
  final Size _minSize;

  const BorderedButton._(
      String label, VoidCallback onTap, TextStyle textStyle, this._minSize,
      {Key? key})
      : super(label, onTap, textStyle, key: key);

  factory BorderedButton.small(String label, VoidCallback onTap, {Key? key}) =>
      BorderedButton._(label, onTap, kLabelSmallTextStyle, kMinSizeSmall,
          key: key);

  factory BorderedButton.medium(String label, VoidCallback onTap, {Key? key}) =>
      BorderedButton._(label, onTap, kLabelMediumTextStyle, kMinSizeMedium,
          key: key);

  factory BorderedButton.large(String label, VoidCallback onTap, {Key? key}) =>
      BorderedButton._(label, onTap, kLabelLargeTextStyle, kMinSizeLarge,
          key: key);

  @override
  Widget build(BuildContext context) => OutlinedButton(
        onPressed: onTap,
        child: Padding(
          padding: kButtonMargins,
          child: Text(label.toUpperCase()),
        ),
        style: ButtonStyle(
          alignment: Alignment.center,
          foregroundColor: MaterialStateProperty.resolveWith<Color>(
            (Set<MaterialState> states) {
              if (states.contains(MaterialState.disabled))
                return kDisabledColor;
              else if (states.contains(MaterialState.hovered))
                return kHoverColor;
              return kNormalColor;
            },
          ),
          overlayColor: MaterialStateProperty.all<Color>(Colors.transparent),
          side: MaterialStateProperty.resolveWith<BorderSide>(
              (Set<MaterialState> states) {
            if (states.contains(MaterialState.disabled))
              return kDisabledBorder;
            else if (states.contains(MaterialState.hovered))
              return kHoveredBorder;
            return kNormalBorder;
          }),
          textStyle: MaterialStateProperty.all<TextStyle>(textStyle),
          shape:
              MaterialStateProperty.all<RoundedRectangleBorder>(kBorderCorner),
          minimumSize: MaterialStateProperty.all<Size>(_minSize),
          padding: MaterialStateProperty.all<EdgeInsets>(EdgeInsets.all(0)),
        ),
      );
}
