// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';
import 'package:oobe/src/states/oobe_state.dart';

/// Width of the password field widget.
const double kOobeBodyFieldWidth = 492;

/// Defines a widget to create account password.
class Login extends StatelessWidget {
  final OobeState oobe;

  final _formState = GlobalKey<FormState>();
  final _passwordController = TextEditingController();
  final _showPassword = false.asObservable();

  Login(this.oobe);

  @override
  Widget build(BuildContext context) {
    return Material(
      color: Color.fromARGB(0xff, 0x0c, 0x0c, 0x0c),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // Header: Fuchsia logo and welcome.
          SizedBox(
            height: 200,
            child: Row(
              mainAxisAlignment: MainAxisAlignment.center,
              crossAxisAlignment: CrossAxisAlignment.end,
              children: [
                // Fuchsia logo.
                Image(
                  image: AssetImage('images/Fuchsia-logo-2x.png'),
                  color: Theme.of(context).colorScheme.primary,
                  width: 24,
                  height: 24,
                ),
                SizedBox(width: 16),
                // Welcome text.
                Text(
                  Strings.fuchsiaWelcome,
                  style: Theme.of(context).textTheme.headline6,
                ),
              ],
            ),
          ),

          // Body: Oobe screens.
          Expanded(
            child: Padding(
              padding: EdgeInsets.all(16),
              child: FocusScope(
                child: Observer(builder: (context) {
                  return Column(
                    crossAxisAlignment: CrossAxisAlignment.center,
                    children: [
                      // Password.
                      Expanded(
                        child: Form(
                          key: _formState,
                          child: Column(
                            mainAxisAlignment: MainAxisAlignment.center,
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              // Title.
                              Text(
                                Strings.login,
                                style: Theme.of(context).textTheme.headline3,
                              ),
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
                                  onFieldSubmitted: (_) {
                                    if (_validate()) {
                                      oobe.login(_passwordController.text);
                                    }
                                  },
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
                              ),
                              SizedBox(height: 40),

                              // Factory reset.
                              SizedBox(
                                width: kOobeBodyFieldWidth,
                                child: Row(
                                  crossAxisAlignment: CrossAxisAlignment.center,
                                  children: [
                                    TextButton(
                                      onPressed: () {},
                                      child: Text(
                                        Strings.factoryDataReset,
                                        style: TextStyle(
                                          decoration: TextDecoration.underline,
                                        ),
                                      ),
                                    ),
                                    SizedBox(width: 10),
                                    IconButton(
                                      icon: Icon(Icons.help_outline),
                                      // TODO(http://fxb/81598): Implement as a
                                      // tooltip.
                                      onPressed: () {},
                                    ),
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
                            // Shutdown button.
                            OutlinedButton(
                              onPressed: oobe.shutdown,
                              child: Text(Strings.shutdown.toUpperCase()),
                            ),
                            SizedBox(width: 24),
                            // Login button.
                            ElevatedButton(
                              onPressed: () => _validate()
                                  ? oobe.login(_passwordController.text)
                                  : null,
                              child: Text(Strings.login.toUpperCase()),
                            ),
                          ],
                        ),
                      ),
                    ],
                  );
                }),
              ),
            ),
          ),
        ],
      ),
    );
  }

  bool _validate() => _formState.currentState?.validate() ?? false;
}
