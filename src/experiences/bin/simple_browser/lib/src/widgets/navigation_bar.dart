// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import '../blocs/webpage_bloc.dart';
import 'history_buttons.dart';
import 'navigation_field.dart';

// TODO(fxb/45264): Make the common factors as part of Ermine central styles.
const _kNavbarHeight = 24.0;
const _kIconSize = 14.0;

enum _LayoutId { historyButtons, url, addTabButton }

class NavigationBar extends StatelessWidget {
  final WebPageBloc bloc;
  final Function newTab;

  const NavigationBar({@required this.bloc, @required this.newTab});

  @override
  Widget build(BuildContext context) => Material(
        child: SizedBox(
          height: _kNavbarHeight,
          child: Stack(
            children: <Widget>[
              Positioned.fill(child: _buildWidgets(context)),
              if (bloc != null)
                Align(
                  alignment: Alignment.bottomCenter,
                  child: _buildLoadingIndicator(),
                ),
            ],
          ),
        ),
      );

  Widget _buildLoadingIndicator() {
    return AnimatedBuilder(
      animation: bloc.isLoadedStateNotifier,
      builder: (context, snapshot) => bloc.isLoadedState
          ? Offstage()
          : SizedBox(
              width: double.infinity,
              height: 4.0,
              child: LinearProgressIndicator(
                backgroundColor: Colors.transparent,
              ),
            ),
    );
  }

  Widget _buildWidgets(BuildContext context) {
    return CustomMultiChildLayout(
      delegate: _LayoutDelegate(),
      children: [
        LayoutId(
          id: _LayoutId.historyButtons,
          child: bloc != null ? HistoryButtons(bloc: bloc) : Container(),
        ),
        LayoutId(
          id: _LayoutId.url,
          child: bloc != null ? NavigationField(bloc: bloc) : Container(),
        ),
        LayoutId(
          id: _LayoutId.addTabButton,
          child: _buildNewTabButton(context),
        ),
      ],
    );
  }

  Widget _buildNewTabButton(BuildContext context) {
    return GestureDetector(
      onTap: newTab,
      child: Align(
        alignment: Alignment.centerRight,
        child: AspectRatio(
          aspectRatio: 1.0,
          child: Padding(
            padding: const EdgeInsets.all(1.0),
            child: Container(
              color: Theme.of(context).accentColor,
              alignment: Alignment.center,
              child: Icon(
                Icons.add,
                key: Key('new_tab'),
                color: Theme.of(context).primaryColor,
                size: _kIconSize,
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _LayoutDelegate extends MultiChildLayoutDelegate {
  @override
  void performLayout(Size size) {
    final historyButtonsSize = layoutChild(
      _LayoutId.historyButtons,
      BoxConstraints.tightFor(height: size.height),
    );
    positionChild(_LayoutId.historyButtons, Offset.zero);
    final newTabButtonSize = layoutChild(
      _LayoutId.addTabButton,
      BoxConstraints(
        minHeight: size.height,
        maxHeight: size.height,
        minWidth: historyButtonsSize.width,
      ),
    );
    positionChild(
      _LayoutId.addTabButton,
      size.topRight(-newTabButtonSize.topRight(Offset.zero)),
    );

    final urlSize = layoutChild(
      _LayoutId.url,
      BoxConstraints.tightFor(
        width: (size.width - historyButtonsSize.width - newTabButtonSize.width)
            .clamp(0.0, size.width),
      ),
    );
    positionChild(
      _LayoutId.url,
      Offset(
        historyButtonsSize.width,
        (size.height - urlSize.height) * 0.5,
      ),
    );
  }

  @override
  bool shouldRelayout(MultiChildLayoutDelegate oldDelegate) => false;
}
