// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';

import 'package:next/src/states/app_state.dart';

/// Defines a widget to display glanceable information like build verison, ip
/// addresses, battery charge or cpu metrics.
class Status extends StatelessWidget {
  final AppState appState;

  const Status(this.appState);

  @override
  Widget build(BuildContext context) {
    return Observer(builder: (_) {
      return Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // IP Address, Build and Battery.
          Expanded(
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Expanded(
                  child: ListTile(
                    minVerticalPadding: 0,
                    title: Text(Strings.network),
                    subtitle: appState.settingsState.networkAddresses.isEmpty
                        ? Text('--')
                        : appState.settingsState.networkAddresses.length == 1
                            ? Text(
                                appState.settingsState.networkAddresses.first,
                                maxLines: 2,
                                style: TextStyle(
                                  overflow: TextOverflow.ellipsis,
                                ),
                              )
                            : Tooltip(
                                message: appState.settingsState.networkAddresses
                                    .join('\n'),
                                child: Text(
                                  appState.settingsState.networkAddresses.first,
                                  maxLines: 2,
                                  style: TextStyle(
                                    overflow: TextOverflow.ellipsis,
                                  ),
                                ),
                              ),
                    onTap: () {},
                  ),
                ),
                Expanded(
                  child: ListTile(
                    minVerticalPadding: 0,
                    title: Text(Strings.build),
                    subtitle: Text(
                      appState.buildVersion,
                      maxLines: 2,
                      style: TextStyle(overflow: TextOverflow.ellipsis),
                    ),
                    onTap: () {},
                  ),
                ),
                Expanded(
                  child: ListTile(
                    minVerticalPadding: 0,
                    title: Text(Strings.power),
                    subtitle: Text('n/a'),
                    onTap: () {},
                  ),
                ),
              ],
            ),
          ),

          // CPU, Memory and Processes.
          Expanded(
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.baseline,
              textBaseline: TextBaseline.alphabetic,
              children: [
                Expanded(
                  child: ListTile(
                    minVerticalPadding: 0,
                    title: Text(Strings.cpu),
                    subtitle: Text('n/a'),
                    onTap: () {},
                  ),
                ),
                Expanded(
                  child: ListTile(
                    minVerticalPadding: 0,
                    title: Text(Strings.memory),
                    subtitle: Text('n/a'),
                    onTap: () {},
                  ),
                ),
                Expanded(
                  child: ListTile(
                    minVerticalPadding: 0,
                    title: Text(Strings.processes),
                    subtitle: Text('n/a'),
                    onTap: () {},
                  ),
                ),
              ],
            ),
          ),
        ],
      );
    });
  }
}
