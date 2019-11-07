// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:internationalization/strings.dart';

import '../blocs/webpage_bloc.dart';
import '../models/webpage_action.dart';

const _kEnabledOpacity = 1.0;
const _kDisabledOpacity = 0.54;
const _kPadding = EdgeInsets.symmetric(horizontal: 4.0);

class HistoryButtons extends StatelessWidget {
  const HistoryButtons({@required this.bloc});

  final WebPageBloc bloc;

  @override
  Widget build(BuildContext context) {
    return Row(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: <Widget>[
        AnimatedBuilder(
            animation: bloc.backStateNotifier,
            builder: (_, __) => _HistoryButton(
                title: Strings.back.toUpperCase(),
                onTap: () => bloc.request.add(GoBackAction()),
                isEnabled: bloc.backState)),
        SizedBox(width: 8.0),
        AnimatedBuilder(
            animation: bloc.forwardStateNotifier,
            builder: (_, __) => _HistoryButton(
                title: Strings.forward.toUpperCase(),
                onTap: () => bloc.request.add(GoForwardAction()),
                isEnabled: bloc.forwardState)),
        SizedBox(width: 8.0),
        AnimatedBuilder(
            animation: bloc.urlNotifier,
            builder: (_, __) => _HistoryButton(
                title: Strings.refresh.toUpperCase(),
                onTap: () => bloc.request.add(RefreshAction()),
                isEnabled: bloc.pageType == PageType.normal)),
      ],
    );
  }
}

class _HistoryButton extends StatelessWidget {
  const _HistoryButton({
    @required this.title,
    @required this.onTap,
    @required this.isEnabled,
  });

  final String title;
  final VoidCallback onTap;
  final bool isEnabled;

  @override
  Widget build(BuildContext context) => GestureDetector(
        onTap: isEnabled ? onTap : null,
        child: Padding(
          padding: _kPadding,
          child: Center(
            child: Opacity(
              opacity: isEnabled ? _kEnabledOpacity : _kDisabledOpacity,
              child: Text(title),
            ),
          ),
        ),
      );
}
