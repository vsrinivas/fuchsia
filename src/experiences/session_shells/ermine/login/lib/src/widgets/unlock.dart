// Copyright 2022 The Fuchsia Authors. All rights reserved.
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

/// Defines a widget for lock screen.
class Unlock extends StatelessWidget {
  final OobeState oobe;

  final _formState = GlobalKey<FormState>();
  final _passwordController = TextEditingController();
  final _showPassword = false.asObservable();
  final _focusNode = FocusNode();

  Unlock(this.oobe);

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
              child: Stack(
                fit: StackFit.expand,
                children: [
                  Align(
                    alignment: FractionalOffset.center,
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        // Title.
                        Padding(
                          padding: EdgeInsets.only(left: 16),
                          child: Text(
                            Strings.unlock,
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
                                        Focus.of(context)
                                            .requestFocus(_focusNode);
                                      }
                                      return null;
                                    },
                                    onFieldSubmitted: (_) {
                                      if (_validate()) {
                                        // TODO(fxb/98139): update calls from login to unlock
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
                                      ? Center(
                                          child: CircularProgressIndicator())
                                      : ElevatedButton(
                                          key: ValueKey('login'),
                                          child: Icon(Icons.arrow_forward),
                                          onPressed: () =>
                                              _validate() && !oobe.wait
                                                  ? oobe.login(
                                                      _passwordController.text)
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
                      ],
                    ),
                  ),
                  // Cancel button.
                  // TODO(fxb/98139): put device to sleep on cancel
                  Align(
                    alignment: FractionalOffset.bottomRight,
                    child: Padding(
                      padding: const EdgeInsets.all(50),
                      child: ElevatedButton.icon(
                        icon: Icon(Icons.cancel),
                        label: Text(Strings.cancel.toUpperCase()),
                        onPressed: () => null,
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
}
