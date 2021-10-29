// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';
import 'package:oobe/src/states/oobe_state.dart';

/// Defines a widget for the final screen when oobe is complete.
class Ready extends StatelessWidget {
  final OobeState oobe;

  const Ready(this.oobe);

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: EdgeInsets.all(16),
      child: FocusScope(
        child: Observer(builder: (context) {
          return Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // Title.
              Text(
                Strings.passwordIsSet,
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.headline3,
              ),

              // Description.
              Container(
                alignment: Alignment.center,
                padding: EdgeInsets.all(24),
                child: SizedBox(
                  width: 600,
                  child: Text(
                    Strings.readyToUse,
                    textAlign: TextAlign.center,
                    style: Theme.of(context)
                        .textTheme
                        .bodyText1!
                        .copyWith(height: 1.55),
                  ),
                ),
              ),

              // Empty.
              Expanded(child: Container()),

              // Start workstation button.
              Container(
                alignment: Alignment.center,
                padding: EdgeInsets.all(24),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    OutlinedButton(
                      autofocus: true,
                      onPressed: oobe.finish,
                      child: Text(Strings.startWorkstation.toUpperCase()),
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
}
