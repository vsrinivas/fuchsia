// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'package:ermine/src/states/app_state.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';

class AppSwitcher extends StatelessWidget {
  final AppState _app;

  const AppSwitcher(this._app);

  @override
  Widget build(BuildContext context) {
    final foregroundColor = Theme.of(context).colorScheme.onSurface;
    return RepaintBoundary(
      child: Center(
        child: Container(
          height: 176,
          margin: EdgeInsets.symmetric(horizontal: 60),
          decoration: BoxDecoration(
            color: Theme.of(context).bottomAppBarColor,
            border: Border.all(color: foregroundColor),
          ),
          child: ListView.separated(
            scrollDirection: Axis.horizontal,
            padding: EdgeInsets.fromLTRB(16, 0, 16, 24),
            shrinkWrap: true,
            itemCount: _app.views.length,
            itemBuilder: (context, index) {
              final view = _app.views[index];
              return Observer(builder: (context) {
                return SizedBox(
                  width: 136,
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.center,
                    children: [
                      // Switch target indicator
                      Container(
                        width: 54,
                        height: 8,
                        color: view == _app.switchTarget
                            ? foregroundColor
                            : Colors.transparent,
                      ),
                      SizedBox(height: 24),
                      Image.asset(
                        _getIconPath(view.title),
                        width: 72,
                        height: 72,
                        color: foregroundColor,
                      ),
                      if (view == _app.switchTarget) ...[
                        SizedBox(height: 24),
                        // App title
                        Text(
                          view.title,
                          style: Theme.of(context)
                              .textTheme
                              .bodyText1!
                              .copyWith(color: foregroundColor),
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                        ),
                      ],
                    ],
                  ),
                );
              });
            },
            separatorBuilder: (_, __) => const SizedBox(width: 8),
          ),
        ),
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
