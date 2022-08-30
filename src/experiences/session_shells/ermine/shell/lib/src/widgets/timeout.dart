// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'package:ermine/src/states/view_state.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';

/// Defines a widget that displays a loading indicator and timeout UX.
///
/// The loading indicator starts after 0.5 seconds and is shown until:
///  - app has started rendering, or
///  - loading timeout has expired.
///
/// If the timeout has expired, it displays buttons to:
///  - continue to wait, or
///  - close the app.
class LoadTimeout extends StatelessWidget {
  final ViewState view;

  const LoadTimeout(this.view);

  @override
  Widget build(BuildContext context) {
    return Observer(builder: (_) {
      return view.loaded
          ? Offstage()
          : Center(
              child: Container(
                width: 503,
                decoration: BoxDecoration(
                  color: Theme.of(context).bottomAppBarColor,
                  border: Border.all(
                      color: Theme.of(context).colorScheme.onSurface),
                ),
                padding: EdgeInsets.all(24),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    // Loading indicator and title.
                    Row(
                      mainAxisAlignment: MainAxisAlignment.start,
                      children: [
                        CircularProgressIndicator(),
                        SizedBox(width: 24),
                        Expanded(
                          child: Text(
                            view.timeout
                                ? Strings.applicationNotResponding
                                : Strings.loadingApplication(view.title),
                            softWrap: true,
                            maxLines: 2,
                            overflow: TextOverflow.ellipsis,
                            style: Theme.of(context).textTheme.headline6,
                          ),
                        )
                      ],
                    ),

                    // Prompt text, wait and close buttons.
                    if (view.timeout) ...[
                      SizedBox(height: 24),
                      Text(Strings.promptToWaitOrClose(view.title)),
                      SizedBox(height: 24),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.end,
                        children: [
                          OutlinedButton(
                              onPressed: view.wait, child: Text(Strings.wait)),
                          SizedBox(width: 16),
                          OutlinedButton(
                              onPressed: view.close,
                              child: Text(Strings.close)),
                        ],
                      )
                    ],
                  ],
                ),
              ),
            );
    });
  }
}
