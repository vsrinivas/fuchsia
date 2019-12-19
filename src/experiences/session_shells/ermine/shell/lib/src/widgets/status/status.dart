// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:internationalization/strings.dart';
import 'package:quickui/uistream.dart';

import '../../models/status_model.dart';
import '../../utils/styles.dart';
import 'detail_status_entry.dart';
import 'status_button.dart';
import 'status_entry.dart';

const kPadding = 12.0;
const kRowHeight = 28.0;
const kItemHeight = 16.0;
const kIconHeight = 18.0;
const kProgressBarWidth = 87.0;
const kStatusBackgroundColor = Color(0xFF0C0C0C);
const kStatusBorderColor = Color(0xFF262626);
const kDefaultTextStyle = TextStyle(
  fontFamily: 'Roboto Mono',
  fontSize: 11,
  letterSpacing: 0,
  fontWeight: FontWeight.w400,
  color: Colors.white,
);

class Status extends StatelessWidget {
  final StatusModel model;

  const Status({@required this.model});

  @override
  Widget build(BuildContext context) {
    // The [PageController] used to switch between main and detail views.
    final pageController = PageController();

    // Returns the callback to handle [QuickAction] from buttons in spec
    // available from the provided [uiStream].
    ValueChanged<Value> _onChange(UiStream uiStream) {
      return (Value value) {
        // Check if a button with [QuickAction] was clicked.
        final action = _actionFromValue(value);
        if (action & QuickAction.details.$value > 0) {
          model.detailNotifier.value = uiStream;

          pageController.nextPage(
            duration: ErmineStyle.kScreenAnimationDuration,
            curve: ErmineStyle.kScreenAnimationCurve,
          );
          uiStream.update(value);
        } else if ((action & QuickAction.cancel.$value > 0 ||
                action & QuickAction.submit.$value > 0) &&
            pageController.page > 0) {
          model.detailStream?.update(value);
          pageController.previousPage(
            duration: ErmineStyle.kScreenAnimationDuration,
            curve: ErmineStyle.kScreenAnimationCurve,
          );
          model.detailNotifier.value = null;
        } else {
          uiStream?.update(value);
        }
      };
    }

    return PageView(
      controller: pageController,
      physics: NeverScrollableScrollPhysics(),
      children: <Widget>[
        SingleChildScrollView(
          padding: EdgeInsets.all(kPadding),
          child: Column(
            children: <Widget>[
              _ManualStatusEntry(model),
              for (final uiStream in [
                model.datetime,
                model.timezone,
                model.volume,
                model.brightness,
                model.battery,
                model.memory,
                model.bluetooth,
                model.weather,
              ])
                StatusEntry(
                  uiStream: uiStream,
                  onChange: _onChange(uiStream),
                  detailNotifier: model.detailNotifier,
                  // getSpec: spec,
                ),
            ],
          ),
        ),
        DetailStatusEntry(
          model: model,
          onChange: _onChange(null),
        )
      ],
    );
  }
}

class _ManualStatusEntry extends StatelessWidget {
  final StatusModel model;

  const _ManualStatusEntry(this.model);

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: EdgeInsets.only(bottom: kPadding),
      height: kRowHeight,
      child: Row(
        children: <Widget>[
          StatusButton(Strings.restart, model.restartDevice),
          Padding(padding: EdgeInsets.only(right: kPadding)),
          StatusButton(Strings.shutdown, model.shutdownDevice),
          Spacer(),
          StatusButton(Strings.logout, model.logoutSession, Key('logout')),
        ],
      ),
    );
  }
}

int _actionFromValue(Value value) {
  switch (value.$tag) {
    case ValueTag.button:
      return value.button.action;
    case ValueTag.number:
      return value.number.action;
    case ValueTag.text:
      return value.text.action;
    case ValueTag.progress:
      return value.progress.action;
    case ValueTag.input:
      return value.input.action;
    case ValueTag.icon:
      return value.input.action;
    case ValueTag.grid:
      return 0;
    case ValueTag.graph:
      return value.graph.action;
    case ValueTag.list:
      return 0;
  }
  return 0;
}
