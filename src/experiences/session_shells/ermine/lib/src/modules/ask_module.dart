// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_services/services.dart' show StartupContext;
import 'package:fuchsia_logger/logger.dart';

import 'ask_model.dart';
import 'ask_sheet.dart';

void main() {
  setupLogger(name: 'ermine_ask_module');

  AskModel model = AskModel(
    startupContext: StartupContext.fromStartupInfo(),
  )..advertise();

  runApp(AskModule(model: model));
}

class AskModule extends StatelessWidget {
  final AskModel model;

  const AskModule({this.model});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark().copyWith(
        textSelectionColor: Color(0xFFFF8BCB),
      ),
      home: DefaultTextStyle(
        style: Theme.of(context).primaryTextTheme.body1.copyWith(
              fontFamily: 'RobotoMono',
              fontWeight: FontWeight.w400,
              fontSize: 18.0,
              color: Colors.white,
            ),
        child: Align(
          alignment: Alignment.bottomRight,
          child: SizedBox(
            width: 500.0,
            child: AskSheet(model: model),
          ),
        ),
      ),
    );
  }
}
