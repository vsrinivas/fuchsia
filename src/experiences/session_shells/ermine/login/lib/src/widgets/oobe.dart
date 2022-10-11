// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';
import 'package:login/src/states/oobe_state.dart';
import 'package:login/src/widgets/data_sharing.dart';
import 'package:login/src/widgets/password.dart';
import 'package:login/src/widgets/ready.dart';
// import 'package:login/src/widgets/channels.dart';
// import 'package:login/src/widgets/ssh_keys.dart';

/// Defines a widget that handles the OOBE flow.
class Oobe extends StatelessWidget {
  final OobeState oobe;

  const Oobe(this.oobe);

  @override
  Widget build(BuildContext context) {
    return Material(
      color: Color.fromARGB(0xff, 0x0c, 0x0c, 0x0c),
      child: Padding(
        padding: EdgeInsets.symmetric(vertical: 100),
        child: Center(
          child: SizedBox(
            width: kContentWidth,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                // Header: Fuchsia logo and welcome.
                Row(
                  crossAxisAlignment: CrossAxisAlignment.center,
                  children: [
                    // Fuchsia logo.
                    Image(
                      image: AssetImage('images/Fuchsia-logo-2x.png'),
                      color: Colors.white,
                      width: 24,
                      height: 24,
                    ),
                    SizedBox(width: 16),
                    // Welcome text.
                    Flexible(
                        child: Text(
                      Strings.fuchsiaWelcome,
                      style: Theme.of(context).textTheme.subtitle1,
                    )),
                  ],
                ),
                SizedBox(height: 56),

                // Page indicator.
                Row(
                  children: [
                    for (var index = oobe.showDataSharing
                            ? OobeScreen.dataSharing.index
                            : OobeScreen.password.index;
                        index <= OobeScreen.done.index;
                        index++)
                      Padding(
                        padding: EdgeInsets.symmetric(horizontal: 6.0),
                        child: Observer(builder: (context) {
                          return Container(
                            width: 12,
                            height: 12,
                            decoration: BoxDecoration(
                              border: Border.all(color: Colors.white),
                              color: index == oobe.screen.index
                                  ? Colors.white
                                  : null,
                            ),
                          );
                        }),
                      ),
                  ],
                ),
                SizedBox(height: 24),

                // Body: Oobe screens.
                Expanded(
                  child: Observer(builder: (context) {
                    switch (oobe.screen) {
                      case OobeScreen.loading:
                        return Offstage();
                      // case OobeScreen.channel:
                      //   return Channels(oobe);
                      case OobeScreen.dataSharing:
                        return DataSharing(oobe);
                      // case OobeScreen.sshKeys:
                      //   return SshKeys(oobe);
                      case OobeScreen.password:
                        return Password(oobe);
                      case OobeScreen.done:
                        return Ready(oobe);
                    }
                  }),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
