// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//ignore: unused_import
import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';
import 'package:login/src/states/oobe_state.dart';
import 'package:login/src/widgets/details.dart';
import 'package:mobx/mobx.dart';

/// Defines a widget to create account password.
class Password extends StatelessWidget {
  static const int kPasswordLength = 8;
  final OobeState oobe;

  final _formState = GlobalKey<FormState>();
  final _passwordController = TextEditingController();
  final _confirmPasswordController = TextEditingController();
  final _showPassword = true.asObservable();

  Password(this.oobe);

  @override
  Widget build(BuildContext context) => Observer(
        builder: (context) => Details(
          // Header: Title and description.
          title: Strings.accountPasswordTitle,
          description: Strings.accountPasswordDesc(kPasswordLength),

          // Content: Password text form fields and checkbox
          scrollableContent: Form(
            key: _formState,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                // Password.
                SizedBox(
                  width: kContentWidth,
                  child: TextFormField(
                    key: ValueKey('password1'),
                    autofocus: true,
                    autovalidateMode: AutovalidateMode.onUserInteraction,
                    controller: _passwordController,
                    obscureText: !_showPassword.value,
                    decoration: InputDecoration(
                      border: OutlineInputBorder(),
                      labelText: Strings.passwordHint,
                    ),
                    validator: (value) {
                      if (value == null ||
                          value.isEmpty ||
                          value.length < kPasswordLength) {
                        return Strings.accountPasswordInvalid;
                      }
                      return null;
                    },
                  ),
                ),
                SizedBox(height: 40),
                // Re-enter password.
                SizedBox(
                  width: kContentWidth,
                  child: TextFormField(
                    key: ValueKey('password2'),
                    autovalidateMode: AutovalidateMode.onUserInteraction,
                    controller: _confirmPasswordController,
                    obscureText: !_showPassword.value,
                    decoration: InputDecoration(
                      border: OutlineInputBorder(),
                      labelText: Strings.confirmPasswordHint,
                    ),
                    validator: (value) {
                      if (value != _passwordController.text) {
                        return Strings.accountPasswordMismatch;
                      }
                      return null;
                    },
                    onFieldSubmitted: (value) =>
                        _validate() ? oobe.setPassword(value) : null,
                  ),
                ),
                SizedBox(height: 40),
                // Show password checkbox.
                Row(
                  crossAxisAlignment: CrossAxisAlignment.center,
                  children: [
                    SizedBox(
                      width: 32,
                      height: 32,
                      child: Checkbox(
                        onChanged: (value) => runInAction(
                            () => _showPassword.value = value == true),
                        value: _showPassword.value,
                      ),
                    ),
                    SizedBox(height: 24),
                    Flexible(child: Text(Strings.showPassword)),
                  ],
                ),
                SizedBox(height: 40),
                // Show spinning indicator if waiting or api errors,
                // if any.
                SizedBox(
                  height: 40,
                  width: kContentWidth,
                  child: oobe.wait
                      ? Center(child: CircularProgressIndicator())
                      : oobe.authError.isNotEmpty
                          ? Text(
                              oobe.authError,
                              style: TextStyle(color: Colors.red),
                            )
                          : Offstage(),
                ),
              ],
            ),
          ),

          // Buttons: Back and Set Password.
          buttons: [
            OutlinedButton(
              onPressed: oobe.wait ? null : oobe.prevScreen,
              child: Text(Strings.back.toUpperCase()),
            ),
            // Set password button.
            ElevatedButton(
              key: ValueKey('setPassword'),
              onPressed: () => _validate() && !oobe.wait
                  ? oobe.setPassword(_confirmPasswordController.text)
                  : null,
              child: Text(Strings.setPassword.toUpperCase()),
            ),
          ],
        ),
      );

  bool _validate() => _formState.currentState?.validate() ?? false;
}
