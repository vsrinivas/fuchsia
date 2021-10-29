// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';
import 'package:oobe/src/states/oobe_state.dart';
import 'package:oobe/src/widgets/header.dart';
import 'package:oobe/src/widgets/login.dart';

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
  Widget build(BuildContext context) {
    return Padding(
      padding: EdgeInsets.all(16),
      child: FocusScope(
        child: Observer(builder: (context) {
          return Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // Title and description.
              Header(
                title: Strings.accountPasswordTitle,
                description: Strings.accountPasswordDesc(kPasswordLength),
              ),

              // Password.
              Expanded(
                child: Form(
                  key: _formState,
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.center,
                    children: [
                      SizedBox(height: 40),
                      // Password.
                      SizedBox(
                        width: kOobeBodyFieldWidth,
                        child: TextFormField(
                          autofocus: true,
                          controller: _passwordController,
                          obscureText: !_showPassword.value,
                          decoration: InputDecoration(
                            border: OutlineInputBorder(),
                            labelText: Strings.passwordHint,
                          ),
                          validator: (value) {
                            // TODO(http://fxb/81598): Uncomment once
                            // login functionality is ready.
                            // if (value == null ||
                            //     value.isEmpty ||
                            //     value.length < passwordLength) {
                            //   return Strings.accountPasswordInvalid;
                            // }
                            return null;
                          },
                        ),
                      ),
                      SizedBox(height: 40),
                      // Re-enter password.
                      SizedBox(
                        width: kOobeBodyFieldWidth,
                        child: TextFormField(
                          controller: _confirmPasswordController,
                          obscureText: !_showPassword.value,
                          decoration: InputDecoration(
                            border: OutlineInputBorder(),
                            labelText: Strings.confirmPasswordHint,
                          ),
                          validator: (value) {
                            // TODO(http://fxb/81598): Uncomment once
                            // login functionality is ready.
                            // if (value == null ||
                            //     value.isEmpty ||
                            //     value.length < passwordLength) {
                            //   return Strings.accountPasswordInvalid;
                            // }
                            // if (value != _passwordController.text) {
                            //   return Strings.accountPasswordMismatch;
                            // }
                            return null;
                          },
                          onFieldSubmitted: (value) =>
                              _validate() ? oobe.setPassword(value) : null,
                        ),
                      ),
                      SizedBox(height: 40),
                      // Show password checkbox.
                      SizedBox(
                        width: kOobeBodyFieldWidth,
                        child: Row(
                          crossAxisAlignment: CrossAxisAlignment.center,
                          children: [
                            Checkbox(
                              onChanged: (value) =>
                                  _showPassword.value = value == true,
                              value: _showPassword.value,
                            ),
                            SizedBox(height: 40),
                            Text(Strings.showPassword)
                          ],
                        ),
                      )
                    ],
                  ),
                ),
              ),

              // Buttons.
              Container(
                alignment: Alignment.center,
                padding: EdgeInsets.all(24),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    // Back button.
                    OutlinedButton(
                      onPressed: oobe.prevScreen,
                      child: Text(Strings.back.toUpperCase()),
                    ),
                    SizedBox(width: 24),
                    // Set password button.
                    ElevatedButton(
                      onPressed: () => _validate()
                          ? oobe.setPassword(_confirmPasswordController.text)
                          : null,
                      child: Text(Strings.setPassword.toUpperCase()),
                    ),
                  ],
                ),
              ),
            ],
          );
        }),
      ),
    );
  }

  bool _validate() => _formState.currentState?.validate() ?? false;
}
