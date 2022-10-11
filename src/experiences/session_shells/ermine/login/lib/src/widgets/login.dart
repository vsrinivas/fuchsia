// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';
import 'package:login/src/states/oobe_state.dart';
import 'package:mobx/mobx.dart';

/// Width of the password field widget.
const double kOobeBodyFieldWidth = 492;

/// Defines a widget to create account password.
class Login extends StatelessWidget {
  final OobeState oobe;

  final _formState = GlobalKey<FormState>();
  final _passwordController = TextEditingController();
  final _showPassword = false.asObservable();
  final _focusNode = FocusNode();

  Login(this.oobe);

  @override
  Widget build(BuildContext context) {
    return Material(
      color: Color.fromARGB(0xff, 0x0c, 0x0c, 0x0c),
      child: Center(
        child: FocusScope(
          child: Observer(builder: (context) {
            return Form(
              onChanged: oobe.resetAuthError,
              key: _formState,
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  // Title.
                  Padding(
                    padding: EdgeInsets.only(left: 16),
                    child: Text(
                      Strings.login,
                      style: Theme.of(context).textTheme.headline3,
                    ),
                  ),
                  SizedBox(height: 36),
                  // Password.
                  SizedBox(
                    width: kOobeBodyFieldWidth,
                    height: 92,
                    child: Padding(
                      padding: EdgeInsets.only(left: 16),
                      child: Row(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Expanded(
                            child: TextFormField(
                              key: ValueKey('password'),
                              focusNode: _focusNode,
                              autofocus: true,
                              autovalidateMode:
                                  AutovalidateMode.onUserInteraction,
                              controller: _passwordController,
                              obscureText: !_showPassword.value,
                              decoration: InputDecoration(
                                border: OutlineInputBorder(),
                                labelText: Strings.passwordHint,
                                errorText: oobe.authError.isNotEmpty
                                    ? oobe.authError
                                    : null,
                              ),
                              validator: (value) {
                                if (value == null || value.isEmpty) {
                                  return Strings.accountPasswordInvalid;
                                }
                                if (oobe.authError.isNotEmpty) {
                                  Focus.of(context).requestFocus(_focusNode);
                                }
                                return null;
                              },
                              onFieldSubmitted: (_) {
                                if (_validate()) {
                                  oobe.login(_passwordController.text);
                                }
                              },
                            ),
                          ),
                          SizedBox(width: 16),
                          Container(
                            width: 56,
                            height: 56,
                            color: oobe.wait
                                ? Theme.of(context).disabledColor
                                : Colors.white,
                            child: oobe.wait
                                ? Center(child: CircularProgressIndicator())
                                : ElevatedButton(
                                    key: ValueKey('login'),
                                    child: Icon(Icons.arrow_forward),
                                    onPressed: () => _validate() && !oobe.wait
                                        ? oobe.login(_passwordController.text)
                                        : null,
                                  ),
                          ),
                        ],
                      ),
                    ),
                  ),

                  // Show password checkbox.
                  SizedBox(
                    width: kOobeBodyFieldWidth,
                    child: Row(
                      crossAxisAlignment: CrossAxisAlignment.center,
                      children: [
                        Checkbox(
                          onChanged: (value) => runInAction(
                              () => _showPassword.value = value == true),
                          value: _showPassword.value,
                        ),
                        SizedBox(height: 40),
                        Text(Strings.showPassword)
                      ],
                    ),
                  ),
                  SizedBox(height: 144),

                  Container(
                    alignment: Alignment.centerLeft,
                    padding: EdgeInsets.only(left: 16),
                    width: kOobeBodyFieldWidth,
                    child: TextButton(
                      style: TextButton.styleFrom(padding: EdgeInsets.zero),
                      onPressed: _confirmFactoryReset,
                      child: Container(
                        padding: EdgeInsets.only(bottom: 1),
                        decoration: BoxDecoration(
                          border: Border(
                            bottom: BorderSide(
                              color: Colors.white,
                              width: 1,
                            ),
                          ),
                        ),
                        child: Text(Strings.factoryDataReset),
                      ),
                    ),
                  ),
                ],
              ),
            );
          }),
        ),
      ),
    );
  }

  bool _validate() => _formState.currentState?.validate() ?? false;

  void _confirmFactoryReset() {
    oobe.showDialog(AlertDialogInfo(
      title: Strings.factoryDataResetTitle,
      body: Strings.factoryDataResetPrompt,
      actions: [Strings.cancel, Strings.eraseAndReset],
      width: 728,
      onAction: (action) {
        if (action == Strings.eraseAndReset) {
          oobe.factoryReset();
        }
      },
    ));
  }
}
