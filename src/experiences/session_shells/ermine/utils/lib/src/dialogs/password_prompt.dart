// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_utils/ermine_utils.dart';
import 'package:internationalization/strings.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:mobx/mobx.dart';

/// Defines a widget to collect password in a TextFormField and used as the
/// content of an AlertDialog.
class PasswordPrompt extends StatelessWidget {
  final PasswordDialogInfo info;
  final _passwordController = TextEditingController();
  final _showPassword = false.asObservable();

  PasswordPrompt(this.info);

  @override
  Widget build(BuildContext context) {
    return Observer(builder: (context) {
      return Column(
        mainAxisSize: MainAxisSize.min,
        mainAxisAlignment: MainAxisAlignment.center,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // Password prompt.
          SizedBox(height: 52, width: 440),
          Text(info.prompt),
          SizedBox(height: 40),

          // Password text field.
          TextFormField(
            key: ValueKey('password'),
            autofocus: true,
            autovalidateMode: AutovalidateMode.onUserInteraction,
            controller: _passwordController,
            obscureText: !_showPassword.value,
            decoration: InputDecoration(
              border: OutlineInputBorder(),
              labelText: Strings.passwordHint,
            ),
            validator: info.validator,
            onFieldSubmitted: (text) {
              if (info.validator?.call(text) == null) {
                Form.of(context).save();
                Navigator.pop(context, info.defaultAction ?? info.actions.last);
              }
            },
            onSaved: info.onSubmit,
          ),
          Container(
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.center,
              children: [
                Checkbox(
                  onChanged: (value) =>
                      runInAction(() => _showPassword.value = value == true),
                  value: _showPassword.value,
                ),
                Text(Strings.showPassword)
              ],
            ),
          ),
        ],
      );
    });
  }
}
