// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_ui/ermine_ui.dart';
import 'package:flutter/material.dart';
import 'package:internationalization/strings.dart';

import '../../models/app_model.dart';
import '../../models/oobe_model.dart';
import '../../utils/styles.dart';

/// Defines a widget that holds the [Oobe] widget and manages its animation.
class OobeContainer extends StatelessWidget {
  final AppModel model;

  const OobeContainer({@required this.model});

  @override
  Widget build(BuildContext context) {
    return Container(child: Oobe(model: OobeModel(onFinished: model.exitOobe)));
  }
}

/// Defines a class that displays the Oobe system overlay.
class Oobe extends StatelessWidget {
  final OobeModel model;

  const Oobe({@required this.model});

  @override
  Widget build(BuildContext context) {
    return Column(
      children: <Widget>[
        buildHeader(),
        buildBody(),
        buildFooter(),
      ],
    );
  }

  @visibleForTesting
  Widget buildHeader() => Container(
      margin: EdgeInsets.only(
          top: ErmineStyle.kOobeHeaderTopMargin,
          bottom: ErmineStyle.kOobeHeaderBottomMargin),
      child:
          Row(mainAxisAlignment: MainAxisAlignment.center, children: <Widget>[
        Padding(
          padding:
              EdgeInsets.only(right: ErmineStyle.kOobeHeaderElementsPadding),
          child: ErmineIcons.fuchsiaLogo.copyWith(
              color: ErmineColors.fuchsia300, size: ErmineStyle.kOobeLogoSize),
        ),
        Text(
          Strings.fuchsiaWelcome.toUpperCase(),
          style: ErmineTextStyles.headline3,
        ),
      ]));

  @visibleForTesting
  Widget buildBody() => Expanded(
        child: AnimatedBuilder(
          animation: model.currentItem,
          builder: (context, _) => model.oobeItems[model.currentItem.value],
        ),
      );

  @visibleForTesting
  Widget buildFooter() => AnimatedBuilder(
      animation: model.currentItem,
      builder: (context, _) => Container(
          margin: EdgeInsets.symmetric(vertical: ErmineStyle.kOobeFooterMargin),
          child: Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: <Widget>[
                for (int i = 0; i < model.oobeItems.length; i++)
                  Container(
                    margin: EdgeInsets.symmetric(
                        horizontal: ErmineStyle.kOobeFooterSquareSize / 2),
                    width: ErmineStyle.kOobeFooterSquareSize,
                    height: ErmineStyle.kOobeFooterSquareSize,
                    decoration: BoxDecoration(
                      border: Border.all(
                        color: ErmineColors.grey100,
                        width: 1,
                      ),
                      color: i == model.currentItem.value
                          ? ErmineColors.grey100
                          : Colors.transparent,
                    ),
                  ),
              ])));
}
