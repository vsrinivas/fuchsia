// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:next/src/states/app_state.dart';

/// Defines a widget to list all launchable application entries.
class AppLauncher extends StatelessWidget {
  final AppState app;

  const AppLauncher(this.app);

  @override
  Widget build(BuildContext context) {
    return RepaintBoundary(
      child: ListView.builder(
        padding: EdgeInsets.symmetric(vertical: 8),
        itemCount: app.appLaunchEntries.length,
        itemBuilder: (context, index) {
          final item = app.appLaunchEntries[index];
          return Observer(builder: (context) {
            return ListTile(
              contentPadding: EdgeInsets.symmetric(horizontal: 24),
              autofocus: index == 0,
              leading: Image(
                image: AssetImage(item['icon'] ?? 'images/Default-icon-2x.png'),
                color: _isEnabled(item)
                    ? Theme.of(context).colorScheme.secondary
                    : Theme.of(context).disabledColor,
                width: 32,
                height: 32,
              ),
              title: Text(item['title']!),
              trailing: _isLoading(item)
                  ? SizedBox(
                      width: 24,
                      height: 24,
                      child: CircularProgressIndicator(),
                    )
                  : null,
              enabled: _isEnabled(item),
              onTap: () => app.launch([item['title']!, item['url']!]),
            );
          });
        },
      ),
    );
  }

  bool _isLoading(Map<String, String> item) {
    final reversedViews = app.views.reversed;
    for (final view in reversedViews) {
      if (view.title == item['title']!) {
        return !view.ready.value;
      }
    }
    return false;
  }

  bool _isEnabled(Map<String, String> item) {
    return item['url'] != null && !_isLoading(item);
  }
}
