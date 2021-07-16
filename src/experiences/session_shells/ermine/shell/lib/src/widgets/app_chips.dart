// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/states/app_state.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';

/// A list of chips that represent currently running apps.
class AppChips extends StatelessWidget {
  final AppState _app;

  const AppChips(this._app);

  @override
  Widget build(BuildContext context) {
    return RepaintBoundary(
      child: Observer(
        builder: (context) {
          return ListView.separated(
            padding: EdgeInsets.symmetric(vertical: 24),
            itemCount: _app.views.length,
            itemBuilder: (context, index) {
              final view = _app.views[index];
              return ListTile(
                minLeadingWidth: 0,
                horizontalTitleGap: 0,
                contentPadding: EdgeInsets.zero,

                // Active view indicator.
                leading: Container(
                  width: 36,
                  alignment: Alignment.centerLeft,
                  child: Container(
                    width: 8,
                    height: 54.0,
                    color: _app.topView.value == view
                        ? Theme.of(context).colorScheme.onSurface
                        : null,
                  ),
                ),

                // Icon and title.
                title: Column(
                  crossAxisAlignment: CrossAxisAlignment.center,
                  children: [
                    IconButton(
                      icon: Image.asset(
                        _getIconPath(view.title),
                        color: Theme.of(context).colorScheme.onSurface,
                      ),
                      iconSize: 48,
                      splashRadius: 8,
                      tooltip: view.title,
                      onPressed: () => _app.switchView([view]),
                    ),
                    Tooltip(
                      message: view.title,
                      child: Text(
                        view.title,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                    ),
                  ],
                ),

                // Close button.
                trailing: Container(
                  width: 36,
                  alignment: Alignment.topLeft,
                  child: Focus(
                    // Prevent close button from automatically gaining focus.
                    // Requires pointer to press it.
                    descendantsAreFocusable: false,
                    child: IconButton(
                      icon: Icon(Icons.close),
                      constraints: BoxConstraints.tight(Size(18, 18)),
                      padding: EdgeInsets.all(0),
                      iconSize: 16,
                      splashRadius: 8,
                      onPressed: view.close,
                    ),
                  ),
                ),

                onTap: () => _app.switchView([view]),
              );
            },
            separatorBuilder: (context, index) => const SizedBox(height: 32),
          );
        },
      ),
    );
  }

  String _getIconPath(String title) {
    final entry = _app.appLaunchEntries.firstWhere(
      (e) => e['title'] == title,
      orElse: () => {},
    );
    return entry['icon'] ?? 'images/Default-icon-2x.png';
  }
}
