// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'button.dart';
import 'layout.dart';

/// Button with solid background color and no borders.
class FilledButton extends ErmineButton {
  final Size _minSize;

  const FilledButton._(
      String label, VoidCallback onTap, TextStyle textStyle, this._minSize,
      {Key? key})
      : super(label, onTap, textStyle, key: key);

  factory FilledButton.small(String label, VoidCallback onTap, {Key? key}) =>
      FilledButton._(label, onTap, kLabelSmallTextStyle, kMinSizeSmall,
          key: key);

  factory FilledButton.medium(String label, VoidCallback onTap, {Key? key}) =>
      FilledButton._(label, onTap, kLabelMediumTextStyle, kMinSizeMedium,
          key: key);

  factory FilledButton.large(String label, VoidCallback onTap, {Key? key}) =>
      FilledButton._(label, onTap, kLabelLargeTextStyle, kMinSizeLarge,
          key: key);

  @override
  Widget build(BuildContext context) => ElevatedButton(
        onPressed: onTap,
        child: Padding(
          padding: kButtonMargins,
          child: Text(label.toUpperCase()),
        ),
        style: ButtonStyle(
          alignment: Alignment.center,
          backgroundColor: MaterialStateProperty.resolveWith<Color>(
            (Set<MaterialState> states) {
              if (states.contains(MaterialState.disabled))
                return kDisabledColor;
              return kNormalColor;
            },
          ),
          foregroundColor:
              MaterialStateProperty.all<Color>(kNormalInverseColor),
          overlayColor: MaterialStateProperty.all<Color>(kHoverColor),
          textStyle: MaterialStateProperty.all<TextStyle>(textStyle),
          shape:
              MaterialStateProperty.all<RoundedRectangleBorder>(kBorderCorner),
          minimumSize: MaterialStateProperty.all<Size>(_minSize),
          padding: MaterialStateProperty.all<EdgeInsets>(EdgeInsets.all(0)),
          elevation: MaterialStateProperty.all<double>(0.0),
          shadowColor: MaterialStateProperty.all<Color>(Colors.transparent),
        ),
      );
}
