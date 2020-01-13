// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../../models/app_model.dart';
import '../../utils/elevations.dart';
import '../../utils/styles.dart';
import '../../widgets/ask/ask.dart';
import '../../widgets/status/status.dart';
import '../../widgets/story/thumbnails.dart';

/// Defines a widget that holds the [Overview] widget and manages its animation.
class OverviewContainer extends StatelessWidget {
  final AppModel model;

  const OverviewContainer({@required this.model});

  @override
  Widget build(BuildContext context) {
    return Overview(model: model);
  }
}

/// Defines a class that display the Overview system overlay.
///
/// Embeds a list of all stories, Ask and Status.
class Overview extends StatelessWidget {
  final AppModel model;

  const Overview({@required this.model});

  @override
  Widget build(BuildContext context) {
    return Material(
      color: ErmineStyle.kBackgroundColor,
      elevation: Elevations.systemOverlayElevation,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: <Widget>[
          Expanded(
            flex: 2,
            child: Container(
              decoration: BoxDecoration(
                border: Border(
                  right: BorderSide(width: 1, color: Colors.white),
                ),
              ),
              constraints: BoxConstraints.expand(),
              child: Thumbnails(model: model.clustersModel, overview: true),
            ),
          ),
          Expanded(
            child: Column(
              children: <Widget>[
                Expanded(
                  child: Container(
                    padding: ErmineStyle.kOverviewElementPadding.copyWith(
                      bottom: 0,
                    ),
                    decoration: BoxDecoration(
                      border: Border(
                        bottom: BorderSide(width: 1, color: Colors.white),
                      ),
                    ),
                    child: Ask(
                      suggestionService: model.suggestions,
                      onDismiss: () => model.overviewVisibility.value = false,
                      unbounded: true,
                    ),
                  ),
                ),
                Expanded(
                  child: Container(
                    padding: ErmineStyle.kOverviewElementPadding,
                    child: Status(model: model.status),
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
