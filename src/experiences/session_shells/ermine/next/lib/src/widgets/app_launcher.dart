// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:next/src/states/app_state.dart';

/// Defines a widget to list all launchable application entries.
class AppLauncher extends StatelessWidget {
  final AppState appState;

  const AppLauncher(this.appState);

  @override
  Widget build(BuildContext context) {
    return RepaintBoundary(
      child: ListView.builder(
        padding: EdgeInsets.symmetric(vertical: 8),
        itemCount: appState.appLaunchEntries.length,
        itemBuilder: (context, index) {
          final item = appState.appLaunchEntries[index];
          return ListTile(
            contentPadding: EdgeInsets.symmetric(horizontal: 24),
            autofocus: index == 0,
            leading: Image(
              image: AssetImage(item['icon']!),
              color: (item['url'] != null)
                  ? Theme.of(context).colorScheme.secondary
                  : Theme.of(context).disabledColor,
              width: 32,
              height: 32,
            ),
            title: Text(item['title']!),
            enabled: item['url'] != null,
            onTap: () => appState.launch([item['title']!, item['url']!]),
          );
        },
      ),
    );
  }
}
