// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'layout.dart';

/// Text input field in the Terminal Chic style for Ermine.
///
/// Each fields does the same thing that the equivalent field in the Flutter's
/// [TextField] does. Some fields like the ones for changing colors are immutable
/// and therefore, not provided by [ErmineTextField].
class ErmineTextField extends StatelessWidget {
  final TextEditingController controller;
  final double? width;
  final EdgeInsetsGeometry contentPadding;
  final String labelText;
  final String hintText;
  final FocusNode? focusNode;
  final bool autofocus;
  final bool enabled;
  final bool enableInteractiveSelection;
  final bool expands;
  final bool obscureText;
  final String obscuringCharacter;
  final String? restorationId;
  final int? minLines;
  final int? maxLines;
  final int? maxLength;
  final bool readOnly;

  final ValueChanged<String>? onChanged;
  final VoidCallback? onEditingComplete;
  final ValueChanged<String>? onSubmitted;
  final EdgeInsets? scrollPadding;
  final ScrollPhysics? scrollPhysics;
  final ScrollController? scrollController;

  const ErmineTextField({
    required this.controller,
    this.width,
    this.contentPadding = kFieldPaddings,
    this.labelText = '',
    this.hintText = '',
    this.focusNode,
    this.autofocus = false,
    this.enabled = true,
    this.enableInteractiveSelection = true,
    this.expands = false,
    this.obscureText = false,
    this.obscuringCharacter = '*',
    this.restorationId,
    this.minLines,
    this.maxLines = 1,
    this.maxLength,
    this.readOnly = false,
    this.onChanged,
    this.onEditingComplete,
    this.onSubmitted,
    this.scrollPadding,
    this.scrollPhysics,
    this.scrollController,
    Key? key,
  }) : super(key: key);

  @override
  Widget build(BuildContext context) => Row(
        crossAxisAlignment: (maxLines! > 1)
            ? CrossAxisAlignment.start
            : CrossAxisAlignment.center,
        children: [
          if (labelText.isNotEmpty)
            Padding(
              padding: EdgeInsets.only(
                top: (maxLines! > 1) ? contentPadding.vertical / 2 : 0,
                right: kLabelToFieldGap,
              ),
              child: Text(labelText, style: kLabelTextStyle),
            ),
          (width != null)
              ? _buildInputFieldWithHint()
              : Expanded(
                  child: _buildInputFieldWithHint(),
                ),
        ],
      );

  Widget _buildInputFieldWithHint() => Stack(
        alignment: (maxLines! > 1) ? Alignment.topLeft : Alignment.centerLeft,
        children: [
          (width != null)
              ? Container(
                  width: width,
                  child: _buildInputField(),
                )
              : _buildInputField(),
          if (hintText.isNotEmpty) _buildHintText(),
        ],
      );

  Widget _buildInputField() => TextField(
        controller: controller,
        autofocus: autofocus,
        focusNode: focusNode,
        enabled: enabled,
        enableInteractiveSelection: enableInteractiveSelection,
        expands: expands,
        obscureText: obscureText,
        obscuringCharacter: obscuringCharacter,
        restorationId: restorationId,
        maxLines: maxLines,
        maxLength: maxLength,
        onChanged: onChanged,
        onEditingComplete: onEditingComplete,
        onSubmitted: onSubmitted,
        cursorColor: kCursorColor,
        cursorWidth: kCursorWidth,
        cursorHeight: kCursorHeight,
        cursorRadius: Radius.zero,
        showCursor: true,
        decoration: InputDecoration(
          fillColor: kFieldBgColor,
          filled: true,
          border: InputBorder.none,
          enabledBorder: InputBorder.none,
          errorBorder: InputBorder.none,
          disabledBorder: InputBorder.none,
          focusedBorder: InputBorder.none,
          contentPadding: contentPadding,
        ),
      );

  Widget _buildHintText() => AnimatedBuilder(
        animation: controller,
        builder: (context, child) =>
            controller.text.isEmpty ? child! : Offstage(),
        child: Padding(
          padding: EdgeInsets.only(
            top: (maxLines! > 1) ? contentPadding.vertical / 2 : 0,
            left:
                contentPadding.horizontal / 2 + kCursorWidth + kCursorToHintGap,
          ),
          child: Text(hintText, style: kHintTextStyle),
        ),
      );
}
