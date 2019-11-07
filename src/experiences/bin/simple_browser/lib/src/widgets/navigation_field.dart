// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:internationalization/strings.dart';

import '../blocs/webpage_bloc.dart';
import '../models/webpage_action.dart';

class NavigationField extends StatefulWidget {
  const NavigationField({@required this.bloc});
  final WebPageBloc bloc;

  @override
  _NavigationFieldState createState() => _NavigationFieldState();
}

class _NavigationFieldState extends State<NavigationField> {
  final _focusNode = FocusNode();
  final _controller = TextEditingController();

  @override
  void initState() {
    super.initState();
    _focusNode.addListener(_onFocusChange);
    _setupBloc(null, widget);
  }

  @override
  void dispose() {
    _setupBloc(widget, null);
    _controller.dispose();
    _focusNode
      ..removeListener(_onFocusChange)
      ..dispose();
    super.dispose();
  }

  @override
  void didUpdateWidget(NavigationField oldWidget) {
    super.didUpdateWidget(oldWidget);
    _setupBloc(oldWidget, widget);
    _updateFocus();
  }

  void _setupBloc(NavigationField oldWidget, NavigationField newWidget) {
    if (oldWidget?.bloc != newWidget?.bloc) {
      oldWidget?.bloc?.urlNotifier?.removeListener(_onUrlChanged);
      widget?.bloc?.urlNotifier?.addListener(_onUrlChanged);
      _controller.text = newWidget?.bloc?.url;
    }
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
  Widget build(BuildContext context) => TextField(
        focusNode: _focusNode,
        autofocus: _controller.text.isEmpty,
        controller: _controller,
        cursorWidth: 8,
        cursorRadius: Radius.zero,
        cursorColor: Colors.black,
        textAlign: TextAlign.center,
        keyboardType: TextInputType.url,
        decoration: InputDecoration(
          contentPadding: EdgeInsets.zero,
          // In general, do not use space characters to move graphical elements
          // around, or you are going to have a bad time. :)
          hintText: '     ${Strings.search.toUpperCase()}',
          border: InputBorder.none,
          isDense: true,
        ),
        onSubmitted: (value) =>
            widget.bloc.request.add(NavigateToAction(url: value)),
        textInputAction: TextInputAction.go,
      );
}
