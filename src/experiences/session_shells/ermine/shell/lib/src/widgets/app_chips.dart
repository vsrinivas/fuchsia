// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/states/app_state.dart';
import 'package:ermine/src/states/view_state.dart';
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
              return Row(
                crossAxisAlignment: CrossAxisAlignment.center,
                children: [
                  (_app.topView.value == view)
                      ? _TopViewIndicator()
                      : SizedBox(width: 8),
                  Expanded(
                    child: Center(
                      child: _AppChip(_app, view, _getIconPath(view.title)),
                    ),
                  ),
                  SizedBox(width: 8),
                ],
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

class _TopViewIndicator extends StatelessWidget {
  @override
  Widget build(BuildContext context) => Container(
        width: 8,
        height: 54.0,
        color: Theme.of(context).colorScheme.secondary,
      );
}

class _AppChip extends StatelessWidget {
  final AppState _app;
  final ViewState _view;
  final String _iconPath;

  const _AppChip(this._app, this._view, this._iconPath);

  @override
  Widget build(BuildContext context) => GestureDetector(
        onTap: () {
          _app.switchView([_view]);
          _app.hideOverlay();
        },
        child: Wrap(
          direction: Axis.vertical,
          crossAxisAlignment: WrapCrossAlignment.center,
          spacing: 16,
          children: [
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                SizedBox(width: 24),
                Image.asset(
                  _iconPath,
                  width: 48,
                  height: 48,
                  color: Theme.of(context).colorScheme.secondary,
                ),
                SizedBox(width: 8),
                IconButton(
                  icon: Icon(Icons.close),
                  constraints: BoxConstraints.tight(Size(18, 18)),
                  padding: EdgeInsets.all(0),
                  iconSize: 16,
                  splashRadius: 8,
                  color: Theme.of(context).colorScheme.secondary,
                  onPressed: _view.close,
                ),
              ],
            ),
            SizedBox(
              width: 140,
              child: Center(
                child: Tooltip(
                  message: _view.title,
                  child: Text(
                    _view.title,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
              ),
            ),
          ],
        ),
      );
}
