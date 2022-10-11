// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/states/app_state.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';

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
          return Observer(builder: (context) {
            final item = app.appLaunchEntries[index];
            return ListTile(
              key: ValueKey('launchItem-$index'),
              contentPadding: EdgeInsets.symmetric(horizontal: 24),
              autofocus: index == 0,
              leading: Image(
                image: AssetImage(item['icon'] ?? 'images/Default-icon-2x.png'),
                color: _isEnabled(item)
                    ? Theme.of(context).colorScheme.onSurface
                    : Theme.of(context).disabledColor,
                width: 32,
                height: 32,
              ),
              title: Text(item['title']!),
              subtitle: !_isLoading(item) && _hasError(item['url'])
                  ? Tooltip(
                      message: app.errors[item['url']!]![1],
                      child: Text(app.errors[item['url']!]![0],
                          style: TextStyle(color: Theme.of(context).errorColor),
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis),
                    )
                  : null,
              trailing: _isLoading(item)
                  ? SizedBox(
                      width: 24,
                      height: 24,
                      child: CircularProgressIndicator(),
                    )
                  : _hasError(item['url'])
                      ? Icon(
                          Icons.error,
                          size: 24,
                          color: Theme.of(context).errorColor,
                        )
                      : null,
              enabled: _isEnabled(item),
              onTap: () {
                if (!_isLoading(item)) {
                  app.launch(item['title']!, item['url']!,
                      alternateServiceName: item['element_manager_name']);
                }
              },
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
        return !view.loading;
      }
    }
    return false;
  }

  bool _isEnabled(Map<String, String> item) => item['url'] != null;

  bool _hasError(String? url) {
    if (url != null) {
      return app.errors.containsKey(url);
    }
    return false;
  }
}
