// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(apwilson): REMOVE THIS ONCE WE HAVE A PROPER IME ON FUCHSIA!

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'blinking_cursor.dart';

const int _kHidUsageKeyboardReturn = 40;
const int _kHidUsageKeyboardBackspace = 42;

const Duration _kCursorDuration = const Duration(milliseconds: 500);

/// A fuchsia-compatible [TextField] replacement.
///
/// When the current platform is Fuchsia, it uses the [RawKeyboardTextField]
/// using the [RawKeyboardListener].
///
/// Otherwise, it fallbacks to the regular [TextField] widget.
///
/// Most parameters are taken from the [TextField] widget, but not all of them.
class FuchsiaCompatibleTextField extends StatelessWidget {
  /// Creates a new instance of [FuchsiaCompatibleTextField].
  FuchsiaCompatibleTextField({
    Key key,
    this.controller,
    this.focusNode,
    this.decoration,
    this.style,
    this.obscureText: false,
    this.onChanged,
    this.onSubmitted,
  })
      : super(key: key);

  /// Controls the text being edited.
  ///
  /// If null, this widget will creates its own [TextEditingController].
  final TextEditingController controller;

  /// Controls whether this widget has keyboard focus.
  final FocusNode focusNode;

  /// The decoration to show around the text field.
  ///
  /// By default, draws a horizontal line under the input field but can be
  /// configured to show an icon, label, hint text, and error text.
  ///
  /// Set this field to null to remove the decoration entirely (including the
  /// extra padding introduced by the decoration to save space for the labels).
  final InputDecoration decoration;

  /// The style to use for the text being edited.
  ///
  /// This text style is also used as the base style for the [decoration].
  ///
  /// If null, defaults to a text style from the current [Theme].
  final TextStyle style;

  /// Whether to hide the text being edited (e.g., for passwords).
  ///
  /// When this is set to true, all the characters in the input are replaced by
  /// U+2022 BULLET characters (•).
  ///
  /// Defaults to false.
  final bool obscureText;

  /// Called when the text being edited changes.
  final ValueChanged<String> onChanged;

  /// Called when the user indicates that they are done editing the text in the
  /// field.
  final ValueChanged<String> onSubmitted;

  @override
  Widget build(BuildContext context) {
    ThemeData theme = Theme.of(context);
    if (theme.platform == TargetPlatform.fuchsia) {
      return new RawKeyboardTextField(
        controller: controller,
        focusNode: focusNode,
        decoration: decoration,
        style: style,
        obscureText: obscureText,
        onChanged: onChanged,
        onSubmitted: onSubmitted,
      );
    } else {
      return new TextField(
        controller: controller,
        focusNode: focusNode,
        decoration: decoration,
        style: style,
        obscureText: obscureText,
        onChanged: onChanged,
        onSubmitted: onSubmitted,
      );
    }
  }
}

/// An [TextField] replacement implemented using the [RawKeyboardListener].
///
/// This class does not support IME or software heyboard.
class RawKeyboardTextField extends StatefulWidget {
  /// Creates a new instance of [RawKeyboardTextField].
  RawKeyboardTextField({
    Key key,
    this.controller,
    this.focusNode,
    this.decoration,
    this.style,
    this.obscureText: false,
    this.onChanged,
    this.onSubmitted,
  })
      : super(key: key);

  /// Controls the text being edited.
  ///
  /// If null, this widget will creates its own [TextEditingController].
  final TextEditingController controller;

  /// Controls whether this widget has keyboard focus.
  final FocusNode focusNode;

  /// The decoration to show around the text field.
  ///
  /// By default, draws a horizontal line under the input field but can be
  /// configured to show an icon, label, hint text, and error text.
  ///
  /// Set this field to null to remove the decoration entirely (including the
  /// extra padding introduced by the decoration to save space for the labels).
  final InputDecoration decoration;

  /// The style to use for the text being edited.
  ///
  /// This text style is also used as the base style for the [decoration].
  ///
  /// If null, defaults to a text style from the current [Theme].
  final TextStyle style;

  /// Whether to hide the text being edited (e.g., for passwords).
  ///
  /// When this is set to true, all the characters in the input are replaced by
  /// U+2022 BULLET characters (•).
  ///
  /// Defaults to false.
  final bool obscureText;

  /// Called when the text being edited changes.
  final ValueChanged<String> onChanged;

  /// Called when the user indicates that they are done editing the text in the
  /// field.
  final ValueChanged<String> onSubmitted;

  @override
  _RawKeyboardTextFieldState createState() => new _RawKeyboardTextFieldState();
}

class _RawKeyboardTextFieldState extends State<RawKeyboardTextField> {
  FocusNode _focusNode;
  FocusNode get _effectiveFocusNode =>
      config.focusNode ?? (_focusNode ??= new FocusNode());

  TextEditingController _controller;
  TextEditingController get _effectiveController =>
      config.controller ?? _controller;

  String get _displayText => (config.obscureText ?? false)
      ? new String.fromCharCodes(
          new List<int>.filled(_effectiveController.text.length, 0x2022))
      : _effectiveController.text;

  @override
  void initState() {
    super.initState();
    if (config.controller == null) _controller = new TextEditingController();
  }

  @override
  void didUpdateConfig(RawKeyboardTextField oldConfig) {
    if (config.controller == null && oldConfig.controller != null)
      _controller ==
          new TextEditingController.fromValue(oldConfig.controller.value);
    else if (config.controller != null && oldConfig.controller == null)
      _controller = null;
  }

  @override
  void dispose() {
    _focusNode?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final TextEditingController controller = _effectiveController;
    final FocusNode focusNode = _effectiveFocusNode;
    FocusScope.of(context).reparentIfNeeded(focusNode);

    return new GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: _acquireFocus,
      child: new RawKeyboardListener(
        focusNode: focusNode,
        onKey: _handleKey,
        child: new AnimatedBuilder(
          animation: new Listenable.merge(<Listenable>[focusNode, controller]),
          builder: (BuildContext context, Widget _) {
            final Widget editable = _buildEditableText(context);
            if (config.decoration != null) {
              return new InputDecorator(
                decoration: config.decoration,
                baseStyle: config.style,
                isFocused: focusNode.hasFocus,
                isEmpty: controller.value.text.isEmpty,
                child: editable,
              );
            }
            return editable;
          },
        ),
      ),
    );
  }

  Widget _buildEditableText(BuildContext context) {
    final ThemeData themeData = Theme.of(context);
    final TextStyle style = config.style ?? themeData.textTheme.subhead;

    Text text = new Text(_displayText, style: style, maxLines: 1);

    double lineHeight = _getLineHeight(text);

    List<Widget> children = <Widget>[text];
    if (_effectiveFocusNode.hasFocus) {
      children.add(
        new BlinkingCursor(
          color: themeData.textSelectionColor,
          height: lineHeight,
          duration: _kCursorDuration,
        ),
      );
    }

    return new Container(
      height: lineHeight,
      child: new ListView(
        scrollDirection: Axis.horizontal,
        children: children,
      ),
    );
  }

  double _getLineHeight(Text text) {
    TextPainter painter = new TextPainter(
      text: new TextSpan(text: text.data, style: text.style),
      maxLines: text.maxLines,
    );

    return painter.preferredLineHeight;
  }

  void _acquireFocus() {
    FocusScope.of(context).requestFocus(_effectiveFocusNode);
  }

  void _handleKey(RawKeyEvent event) {
    // We're only interested in KeyDown event for now.
    if (event is! RawKeyDownEvent) {
      return;
    }

    assert(event.data is RawKeyEventDataFuchsia);
    RawKeyEventDataFuchsia data = event.data;
    final TextEditingController controller = _effectiveController;

    if (data.codePoint != 0) {
      controller.text =
          controller.text + new String.fromCharCode(data.codePoint);
      _notifyTextChanged(controller.text);
    } else if (data.hidUsage == _kHidUsageKeyboardReturn) {
      if (config.onSubmitted != null) {
        config.onSubmitted(controller.text);
      }
    } else if (data.hidUsage == _kHidUsageKeyboardBackspace) {
      if (controller.text.isNotEmpty) {
        controller.text =
            controller.text.substring(0, controller.text.length - 1);
        _notifyTextChanged(controller.text);
      }
    }
  }

  void _notifyTextChanged(String newText) {
    if (config.onChanged != null) {
      config.onChanged(newText);
    }
  }
}
