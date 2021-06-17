// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

const _kAppEntries = <Map<String, String>>[
  {
    'title': 'Simple Browser',
    'icon': 'images/SimpleBrowser-icon-2x.png',
    'url': 'fuchsia-pkg://fuchsia.com/simple-browser#meta/simple-browser.cmx',
  },
  {
    'title': 'Spinning Square View',
    'icon': 'images/SpinningSquareView-icon-2x.png',
    'url':
        'fuchsia-pkg://fuchsia.com/spinning_square_view#meta/spinning_square_view.cmx',
  },
  {
    'title': 'Terminal',
    'icon': 'images/Terminal-icon-2x.png',
    'url': 'fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cmx',
  },
  {
    'title': 'Chromium',
    'icon': 'images/Chromium-icon-2x.png',
  },
  {
    'title': 'Settings',
    'icon': 'images/Settings-icon-2x.png',
  },
];

/// Defines a widget to list all launchable application entries.
class AppLauncher extends StatelessWidget {
  final void Function(String title, String url) onLaunch;

  const AppLauncher({required this.onLaunch});

  @override
  Widget build(BuildContext context) {
    return RepaintBoundary(
      child: ListView.builder(
        padding: EdgeInsets.symmetric(vertical: 8),
        itemCount: _kAppEntries.length,
        itemBuilder: (context, index) {
          final item = _kAppEntries[index];
          return ListTile(
            contentPadding: EdgeInsets.symmetric(horizontal: 24),
            autofocus: index == 0,
            leading: Image(
              image: AssetImage(item['icon']!),
              width: 32,
              height: 32,
            ),
            title: Text(item['title']!),
            enabled: item['url'] != null,
            onTap: () => onLaunch(item['title']!, item['url']!),
          );
        },
      ),
    );
  }
}
