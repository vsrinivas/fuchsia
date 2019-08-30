// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import '../blocs/webpage_bloc.dart';
import '../models/webpage_action.dart';

const _kBackgroundColor = Colors.black;
const _kBackgroundFocusedColor = Color(0xFFFF8BCB);

const _kTextColor = Colors.white;
const _kTextFocusedColor = Colors.black;

enum _LayoutId { historyButtons, url }

class NavigationBar extends StatefulWidget {
  final WebPageBloc bloc;

  const NavigationBar({this.bloc});

  @override
  _NavigationBarState createState() => _NavigationBarState();
}

class _NavigationBarState extends State<NavigationBar> {
  FocusNode _focusNode;
  TextEditingController _controller;

  @override
  void initState() {
    _focusNode = FocusNode();
    _controller = TextEditingController();
    _focusNode.addListener(_onFocusChange);
    widget.bloc.urlNotifier.addListener(_onUrlChanged);
    super.initState();
  }

  @override
  void dispose() {
    _focusNode
      ..removeListener(_onFocusChange)
      ..dispose();
    _controller.dispose();
    widget.bloc.urlNotifier.removeListener(_onUrlChanged);
    super.dispose();
  }

  @override
  void didUpdateWidget(NavigationBar oldWidget) {
    if (oldWidget.bloc != widget.bloc) {
      oldWidget.bloc.urlNotifier.removeListener(_onUrlChanged);
      widget.bloc.urlNotifier.addListener(_onUrlChanged);
      _controller.text = widget.bloc.url;
      _updateFocus();
    }
    super.didUpdateWidget(oldWidget);
  }

  void _updateFocus() {
    if (_controller.text.isEmpty) {
      FocusScope.of(context).requestFocus(_focusNode);
    } else {
      _focusNode.unfocus();
    }
  }

  void _onFocusChange() {
    if (_focusNode.hasFocus) {
      _controller.selection =
          TextSelection(baseOffset: 0, extentOffset: _controller.text.length);
    }
  }

  void _onUrlChanged() {
    _controller.text = widget.bloc.url;
    _updateFocus();
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: _focusNode,
      builder: (_, child) {
        final focused = _focusNode.hasFocus;
        final textColor = focused ? _kTextFocusedColor : _kTextColor;
        final bgColor = focused ? _kBackgroundFocusedColor : _kBackgroundColor;
        return AnimatedTheme(
          duration: Duration(milliseconds: 100),
          data: ThemeData(
            fontFamily: 'RobotoMono',
            textSelectionColor: textColor.withOpacity(0.38),
            textSelectionHandleColor: textColor,
            hintColor: textColor,
            cursorColor: textColor,
            canvasColor: bgColor,
            accentColor: textColor,
            textTheme: TextTheme(
              body1: TextStyle(color: textColor),
              subhead: TextStyle(color: textColor),
            ),
          ),
          child: child,
        );
      },
      child: Material(
        child: SizedBox(
          height: 26.0,
          child: Stack(
            children: <Widget>[
              Positioned.fill(child: _buildWidgets()),
              Align(
                alignment: Alignment.bottomCenter,
                child: _buildLoadingIndicator(),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildLoadingIndicator() {
    return AnimatedBuilder(
      animation: widget.bloc.isLoadedStateNotifier,
      builder: (context, snapshot) {
        return widget.bloc.isLoadedState
            ? Offstage()
            : SizedBox(
                width: double.infinity,
                height: 4.0,
                child: LinearProgressIndicator(
                  backgroundColor: Colors.transparent,
                ),
              );
      },
    );
  }

  Widget _buildWidgets() {
    return CustomMultiChildLayout(
      delegate: _LayoutDelegate(),
      children: [
        LayoutId(
          id: _LayoutId.historyButtons,
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: <Widget>[
              AnimatedBuilder(
                  animation: widget.bloc.backStateNotifier,
                  builder: (_, __) {
                    return _buildHistoryButton(
                      title: 'BCK',
                      onTap: () => widget.bloc.request.add(GoBackAction()),
                      isEnabled: widget.bloc.backState,
                    );
                  }),
              SizedBox(width: 8.0),
              AnimatedBuilder(
                  animation: widget.bloc.forwardStateNotifier,
                  builder: (_, __) {
                    return _buildHistoryButton(
                      title: 'FWD',
                      onTap: () => widget.bloc.request.add(GoForwardAction()),
                      isEnabled: widget.bloc.forwardState,
                    );
                  }),
            ],
          ),
        ),
        LayoutId(id: _LayoutId.url, child: _buildNavigationField()),
      ],
    );
  }

  Widget _buildHistoryButton({
    @required String title,
    @required VoidCallback onTap,
    @required bool isEnabled,
  }) {
    return GestureDetector(
      onTap: isEnabled ? onTap : null,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 8.0),
        child: Center(
          child: Opacity(
            opacity: isEnabled ? 1.0 : 0.54,
            child: Text(
              title,
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.bold,
              ),
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildNavigationField() {
    return TextField(
      focusNode: _focusNode,
      autofocus: _controller.text.isEmpty,
      controller: _controller,
      cursorWidth: 7,
      cursorRadius: Radius.zero,
      cursorColor: Colors.black,
      textAlign: TextAlign.center,
      keyboardType: TextInputType.url,
      style: TextStyle(fontSize: 14.0),
      decoration: InputDecoration(
        contentPadding: EdgeInsets.zero,
        hintText: '<search>',
        border: InputBorder.none,
        isDense: true,
      ),
      onSubmitted: (value) =>
          widget.bloc.request.add(NavigateToAction(url: value)),
      textInputAction: TextInputAction.go,
    );
  }
}

class _LayoutDelegate extends MultiChildLayoutDelegate {
  @override
  void performLayout(Size size) {
    final buttonsSize = layoutChild(
      _LayoutId.historyButtons,
      BoxConstraints.tightFor(height: size.height),
    );
    positionChild(_LayoutId.historyButtons, Offset.zero);

    final urlSize = layoutChild(
      _LayoutId.url,
      BoxConstraints.tightFor(width: size.width - buttonsSize.width * 2),
    );
    positionChild(
      _LayoutId.url,
      Offset(
        (size.width - urlSize.width) * 0.5,
        (size.height - urlSize.height) * 0.5,
      ),
    );
  }

  @override
  bool shouldRelayout(MultiChildLayoutDelegate oldDelegate) => false;
}
