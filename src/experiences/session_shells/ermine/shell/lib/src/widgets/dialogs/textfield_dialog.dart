// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/widgets/dialogs/dialog.dart' as ermine;
import 'package:flutter/material.dart';
import 'package:internationalization/strings.dart';

/// A dialog that has a [TextField], action buttons, and an optional text
/// description.
class TextfieldDialog extends ermine.Dialog {
  final TextEditingController textController;
  final bool isPassword;
  final String? description;
  final String? fieldLabel;
  final String? fieldHint;
  final Map<String, VoidCallback> buttons;
  final String? errorMessage;
  final bool autoFocus;

  final _isObscure = ValueNotifier(false);

  TextfieldDialog(
      {required this.buttons,
      required this.textController,
      this.isPassword = false,
      this.description,
      this.fieldLabel,
      this.fieldHint,
      this.errorMessage,
      this.autoFocus = true,
      Key? key})
      : super(key: key) {
    if (isPassword) {
      _isObscure.value = true;
    }
  }

  @override
  Widget build(BuildContext context) => AlertDialog(
        title: (description != null) ? Text(description!) : null,
        content: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ValueListenableBuilder<bool>(
              valueListenable: _isObscure,
              builder: (context, isObscure, _) => Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Container(
                    width: 440,
                    child: TextField(
                      controller: textController,
                      maxLines: 1,
                      decoration: InputDecoration(
                        labelText: fieldLabel,
                        border: OutlineInputBorder(
                          borderSide:
                              BorderSide(color: Theme.of(context).dividerColor),
                          borderRadius: BorderRadius.circular(0),
                        ),
                        hintText: fieldHint,
                        errorText: errorMessage,
                      ),
                      autofocus: autoFocus,
                      obscureText: isObscure,
                    ),
                  ),
                  if (isPassword)
                    Row(
                      children: [
                        Checkbox(
                          value: !isObscure,
                          onChanged: (value) =>
                              _isObscure.value = value != true,
                        ),
                        SizedBox(width: 8),
                        Text(
                          Strings.showPassword,
                          style: Theme.of(context).textTheme.bodyText1,
                        ),
                      ],
                    ),
                ],
              ),
            ),
          ],
        ),
        actions: [
          for (final label in buttons.keys)
            TextButton(
              onPressed: buttons[label],
              child: Text(label.toUpperCase()),
            ),
        ],
        insetPadding: EdgeInsets.symmetric(horizontal: 240),
        titlePadding: EdgeInsets.fromLTRB(40, 40, 40, 24),
        contentPadding: EdgeInsets.fromLTRB(40, 0, 40, 0),
        actionsPadding: EdgeInsets.only(right: 40, bottom: 24),
        titleTextStyle: Theme.of(context).textTheme.subtitle1,
      );
}
