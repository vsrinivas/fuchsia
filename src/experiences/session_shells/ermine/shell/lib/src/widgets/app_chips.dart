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
            padding: EdgeInsets.only(top: 8, bottom: 24),
            itemCount: _app.views.length,
            itemBuilder: (context, index) {
              final view = _app.views[index];
              return ListTile(
                minLeadingWidth: 0,
                horizontalTitleGap: 0,
                contentPadding: EdgeInsets.zero,

                // Active view indicator.
                leading: Transform.translate(
                  offset: Offset(0, 12),
                  child: Container(
                    padding: EdgeInsets.only(right: 4),
                    width: 8,
                    height: 54,
                    color: _app.topView == view
                        ? Theme.of(context).colorScheme.onSurface
                        : null,
                  ),
                ),

                // Icon and title.
                title: Column(
                  crossAxisAlignment: CrossAxisAlignment.center,
                  children: [
                    Row(
                      mainAxisSize: MainAxisSize.min,
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        SizedBox(width: 48),
                        // App icon
                        IconButton(
                          icon: Image.asset(
                            _getIconPath(view.title),
                            color: Theme.of(context).colorScheme.onSurface,
                          ),
                          iconSize: 48,
                          padding: EdgeInsets.fromLTRB(0, 16, 0, 16),
                          splashRadius: 1,
                          tooltip: view.title,
                          onPressed: () => _app.switchView(view),
                        ),
                        // Close button
                        Focus(
                          // Prevent close button from automatically gaining focus.
                          // Requires pointer to press it.
                          descendantsAreFocusable: false,
                          child: IconButton(
                            key: ValueKey('appChipClose-$index'),
                            icon: Icon(Icons.close),
                            iconSize: 24,
                            splashRadius: 24,
                            onPressed: view.close,
                          ),
                        ),
                      ],
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
                onTap: () => _app.switchView(view),
              );
            },
            separatorBuilder: (context, index) => const SizedBox(height: 24),
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
