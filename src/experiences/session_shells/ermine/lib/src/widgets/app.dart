// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:intl/intl.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_scenic_flutter/child_view.dart' show ChildView;

import '../models/app_model.dart';
import '../widgets/status.dart';

import 'clusters.dart';
import 'fullscreen_story.dart';

/// Builds the main display of this session shell.
class App extends StatelessWidget {
  final AppModel model;

  const App({@required this.model});

  @override
  Widget build(BuildContext context) => MaterialApp(
        debugShowCheckedModeBanner: false,
        theme: ThemeData(
          textTheme: Theme.of(context).primaryTextTheme.copyWith(
                body1: TextStyle(
                  fontFamily: 'RobotoMono',
                  fontWeight: FontWeight.w400,
                  fontSize: 24.0,
                  color: Colors.white,
                ),
              ),
        ),
        home: Builder(
          builder: (BuildContext context) {
            return Material(
              color: model.backgroundColor,
              child: Stack(
                fit: StackFit.expand,
                overflow: Overflow.visible,
                children: <Widget>[
                  // Current Time.
                  Align(
                    alignment: Alignment.bottomCenter,
                    child: AnimatedBuilder(
                      animation: model.currentTime,
                      builder: (_, __) => Padding(
                        padding: EdgeInsets.only(bottom: 8),
                        child: Text(DateFormat()
                            .add_E()
                            .add_jm()
                            .format(model.currentTime.value)),
                      ),
                    ),
                  ),

                  // Story Clusters.
                  Positioned.fill(
                    child: Clusters(
                      model: model.clustersModel,
                    ),
                  ),

                  // Keyboard shortcuts help button.
                  Align(
                    alignment: Alignment.bottomRight,
                    child: Padding(
                      padding: EdgeInsets.all(8),
                      child: GestureDetector(
                        behavior: HitTestBehavior.translucent,
                        onTap: model.onKeyboard,
                        child: Icon(
                          Icons.keyboard,
                          color: Colors.white,
                        ),
                      ),
                    ),
                  ),

                  // Fullscreen story.
                  Positioned.fill(
                    child: FullscreenStory(model),
                  ),

                  // Scrim to dismiss system overlays.
                  Positioned.fill(
                    child: Listener(
                      behavior: HitTestBehavior.translucent,
                      onPointerDown: (_) => model.onCancel(),
                    ),
                  ),

                  // Keyboard shortcuts help.
                  Center(
                    child: AnimatedBuilder(
                        animation: model.helpVisibility,
                        builder: (context, child) {
                          return model.helpVisibility.value
                              ? Container(
                                  padding: EdgeInsets.all(16),
                                  width: 700,
                                  height: 400,
                                  color: Colors.black,
                                  child: SingleChildScrollView(
                                    child: Text(
                                      model.keyboardShortcuts,
                                      style: Theme.of(context)
                                          .primaryTextTheme
                                          .title
                                          .merge(TextStyle(
                                            fontFamily: 'RobotoMono',
                                            fontSize: 16.0,
                                          )),
                                    ),
                                  ),
                                )
                              : Offstage();
                        }),
                  ),

                  // Ask.
                  Positioned(
                    top: 0,
                    right: 0,
                    bottom: 40,
                    width: 500,
                    child: AnimatedBuilder(
                      animation: Listenable.merge([
                        model.askVisibility,
                        model.askChildViewConnection,
                      ]),
                      builder: (context, child) => !model.askVisibility.value ||
                              model.askChildViewConnection.value == null
                          ? Offstage()
                          : ChildView(
                              connection: model.askChildViewConnection.value,
                            ),
                    ),
                  ),

                  // Status.
                  Positioned(
                    top: 0,
                    height: 315,
                    right: 0,
                    width: 400,
                    child: AnimatedBuilder(
                      animation: model.statusVisibility,
                      builder: (context, _) => model.statusVisibility.value
                          ? Status(model: model.status)
                          : Offstage(),
                    ),
                  ),
                ],
              ),
            );
          },
        ),
      );
}
