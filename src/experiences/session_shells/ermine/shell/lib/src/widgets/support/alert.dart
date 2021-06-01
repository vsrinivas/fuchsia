// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_ui/ermine_ui.dart' as ermine_ui;
import 'package:flutter/material.dart';

import '../../models/app_model.dart';

/// A widget that displays [Alert] widget when Ermine has more than one.
class AlertContainer extends StatelessWidget {
  final AppModel model;

  const AlertContainer({@required this.model});

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
        animation: model.alertsModel,
        builder: (BuildContext context, _) =>
            model.alertVisibility.value ? buildAlertDialog(model) : Offstage(),
      );

  @visibleForTesting
  Widget buildAlertDialog(AppModel model) => Center(
        child: GestureDetector(
          behavior: HitTestBehavior.translucent,
          child: ermine_ui.Alert(
            key: ValueKey(model.alertsModel.currentAlert.title),
            header: model.alertsModel.currentAlert.header,
            title: model.alertsModel.currentAlert.title,
            description: model.alertsModel.currentAlert.description,
            buttons: [
              for (final action in model.alertsModel.currentAlert.actions)
                ermine_ui.FilledButton.small(action.name, action.callback)
            ],
            onClose: model.alertsModel.currentAlert.close,
          ),
        ),
      );
}
