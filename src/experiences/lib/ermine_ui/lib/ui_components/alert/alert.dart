// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../../visual_languages/icons.dart';
import '../button/button.dart';
import 'layout.dart';

/// Pop-up style UI mainly to present a system-wide critical message.
class Alert extends StatelessWidget {
  /// Optional text displayed in the header area of the alert window.
  final String header;

  /// Required text displayed as a title of the alert.
  final String title;

  /// Optional text for description displayed under the title.
  final String description;

  /// Optional widget displayed under the body text for customization purpose.
  final Widget? customWidget;

  /// Optional buttons that trigger an action related to this alert.
  final _buttons = <ErmineButton>[];

  final VoidCallback onClose;

  List<ErmineButton> get buttons => _buttons;

  Alert({
    required this.title,
    required this.onClose,
    this.header = '',
    this.description = '',
    this.customWidget,
    List<ErmineButton> buttons = const <ErmineButton>[],
    Key? key,
  }) : super(key: key) {
    _buttons.addAll(buttons);
  }

  @override
  Widget build(BuildContext context) => IntrinsicWidth(
        child: Container(
          constraints: BoxConstraints(maxWidth: kWindowWidthMax),
          decoration: BoxDecoration(
            color: kBackgroundColor,
            border: Border.all(color: kBorderColor, width: kBorderThickness),
            boxShadow: [kAlertShadow],
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              _buildHeader(),
              _buildBody(),
              if (_buttons.isNotEmpty) _buildButtonRow(),
            ],
          ),
        ),
      );

  Widget _buildHeader() => Column(
        children: [
          Container(
            padding: kHeaderPaddings,
            child: Row(
              children: [
                if (header.isNotEmpty) ...[
                  Expanded(
                    key: ValueKey('alert_header'),
                    child: Center(
                      child: Text(
                        header,
                        style: kHeaderTextStyle,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                    ),
                  ),
                  SizedBox(width: kHeaderTextToIconGap),
                ],
                if (header.isEmpty) const Spacer(),
                GestureDetector(
                  key: ValueKey('alert_close'),
                  onTap: onClose,
                  child: ErmineIcons.close.copyWith(size: kCloseIconSize),
                ),
              ],
            ),
          ),
          if (header.isNotEmpty)
            const Divider(
              key: ValueKey('alert_divider'),
              color: kBorderColor,
              height: kBorderThickness,
              thickness: kBorderThickness,
            ),
        ],
      );

  Widget _buildBody() => Container(
        padding: kBodyPaddings,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              title.toUpperCase(),
              style: kTitleTextStyle,
              maxLines: 3,
              overflow: TextOverflow.ellipsis,
              key: ValueKey('alert_title'),
            ),
            if (description.isNotEmpty) ...[
              SizedBox(height: kTitleToDescriptionGap),
              Text(
                description,
                style: kDescriptionTextStyle,
                maxLines: 5,
                overflow: TextOverflow.ellipsis,
                key: ValueKey('alert_description'),
              ),
            ],
            if (customWidget != null) ...[
              SizedBox(height: kDescriptionToCustomWidgetGap),
              customWidget!
            ],
          ],
        ),
      );

  Widget _buildButtonRow() => Container(
        padding: kButtonRowPaddings,
        child: Row(
          key: ValueKey('alert_button_row'),
          mainAxisAlignment: MainAxisAlignment.end,
          children: [
            for (final button in _buttons) ...[
              button,
              if (button != _buttons.last) SizedBox(width: kButtonToButtonGap),
            ],
          ],
        ),
      );
}
