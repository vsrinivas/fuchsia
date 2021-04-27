// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_ui/ermine_ui.dart';
import 'package:flutter/material.dart';
import 'package:internationalization/strings.dart';

import '../../utils/styles.dart';
import 'oobe_buttons.dart';
import 'oobe_header.dart';

class DataSharing extends StatelessWidget {
  final VoidCallback onBack;
  final VoidCallback onNext;

  const DataSharing({@required this.onBack, @required this.onNext});

  @override
  Widget build(BuildContext context) {
    return Column(children: <Widget>[
      OobeHeader(Strings.dataSharingTitle,
          [DescriptionModel(text: Strings.dataSharingDesc)]),
      // Body.
      Expanded(
        child: Container(
          margin: EdgeInsets.symmetric(
              vertical: ErmineStyle.kOobeBodyVerticalMargins),
          child: Text(
            'Placeholder data sharing consent.',
            textAlign: TextAlign.center,
            style: ErmineTextStyles.headline4,
          ),
        ),
      ),
      OobeButtons([
        OobeButtonModel(Strings.back, onBack),
        OobeButtonModel(Strings.skip, onNext),
      ]),
    ]);
  }
}
