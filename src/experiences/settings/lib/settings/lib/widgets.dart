// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:lib.widgets/widgets.dart';

TextStyle _textStyle(double scale) => TextStyle(
      color: Colors.grey[900],
      fontSize: 24.0 * scale,
      fontWeight: FontWeight.w200,
    );

TextStyle _titleTextStyle(double scale) => TextStyle(
      color: Colors.grey[900],
      fontSize: 48.0 * scale,
      fontWeight: FontWeight.w200,
    );

// Padding that is used as an edge inset for settings lists.
const double _listPadding = 28.0;

Widget _applyStartPadding({@required Widget child, @required double scale}) {
  return Container(
    padding: EdgeInsets.only(left: _listPadding * scale),
    child: child,
  );
}

/// A settings item should have a flexible width but height as specified
abstract class SettingsItem extends StatelessWidget {
  static const double _unscaledHeight = 48.0;

  /// Scaling factor to render widget
  final double scale;

  /// Builds a new settings item with the specified scale.
  const SettingsItem(this.scale);

  /// Total height of a single settings item.
  double get height => _unscaledHeight * scale;
}

/// A list of items such as devices or toggles.
class SettingsItemList extends StatelessWidget {
  /// A list of child items in a settings item
  final Iterable<SettingsItem> items;

  final CrossAxisAlignment crossAxisAlignment;

  /// Constructs a new list with the settings items
  const SettingsItemList(
      {@required this.items,
      this.crossAxisAlignment = CrossAxisAlignment.center});

  @override
  Widget build(BuildContext context) {
    return Column(
        mainAxisSize: MainAxisSize.min,
        children: items.toList(),
        crossAxisAlignment: crossAxisAlignment);
  }
}

/// A single page displayed in a settings app.
class SettingsPage extends StatelessWidget {
  /// Whether or not to display a spinner
  final bool isLoading;

  /// The label to be shown for the entire page
  final String title;

  /// The subsections of the settings page
  final List<SettingsSection> sections;

  /// The scale at which to render the text
  final double scale;

  /// Constructor.
  const SettingsPage({
    @required this.scale,
    this.isLoading = false,
    this.title,
    this.sections,
  });

  @override
  Widget build(BuildContext context) {
    final List<Widget> children = [];

    final verticalInsets = EdgeInsets.only(
        top: _listPadding * scale, bottom: _listPadding * scale);

    if (title != null) {
      children.add(SettingsSection(
        title: title,
        scale: scale,
        child: Offstage(offstage: true),
      ));
    }

    if (isLoading) {
      children.add(Expanded(
        child: Center(
            child: Container(
          width: 64.0,
          height: 64.0,
          child: FuchsiaSpinner(),
        )),
      ));
      return Container(
        padding: verticalInsets,
        child: Column(children: children),
      );
    } else {
      return ListView(
          physics: BouncingScrollPhysics(),
          padding: verticalInsets,
          children: children..addAll(sections));
    }
  }
}

/// Widget that displays a popup which is dismissable by tapping outside of the
/// child widget.
///
/// The child widget should  therefore be smaller than the full size of the screen.
class SettingsPopup extends StatelessWidget {
  /// The child should leave some space for the user to dismiss the popup.
  final Widget child;

  /// Called when user taps outside of displayed popup
  final VoidCallback onDismiss;

  /// Constructor.
  const SettingsPopup({@required this.child, @required this.onDismiss});

  @override
  Widget build(BuildContext context) {
    Widget overlayCancel = Opacity(
        opacity: 0.4,
        child: Material(
            color: Colors.grey[900],
            child: GestureDetector(
              behavior: HitTestBehavior.opaque,
              onTap: onDismiss,
            )));

    return Stack(children: [
      overlayCancel,
      Center(child: child),
    ]);
  }
}

/// A subsection of a settings page, with an optional title.
///
/// All subsections should be fixed height widgets.
class SettingsSection extends StatelessWidget {
  static const SettingsSection _emptySection =
      SettingsSection(scale: 1.0, child: Offstage(offstage: true));

  /// String displayed at the top of the section
  final String title;

  /// Contents of the section
  final Widget child;

  /// Scale at which to render the text
  final double scale;

  /// Whether we are the top section
  final bool topSection;

  const SettingsSection({
    @required this.child,
    @required this.scale,
    this.title,
    this.topSection = true,
  });

  /// Returns an empty section with no title.
  factory SettingsSection.empty() => _emptySection;

  /// Returns a section with just some text describing why the section
  /// has an error.
  ///
  /// Used when the underlying settings are unavailable or not operational.
  factory SettingsSection.error({
    @required double scale,
    @required String description,
    String title,
  }) {
    return SettingsSection(
      scale: scale,
      title: title,
      child: SettingsTile(scale: scale, text: description),
    );
  }

  @override
  Widget build(BuildContext context) {
    if (title != null) {
      return Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Container(
            padding: EdgeInsets.only(
                top: topSection ? 0.0 : _listPadding * scale,
                left: _listPadding * scale,
                right: _listPadding * scale),
            child: Text(title, style: _titleTextStyle(scale))),
        child
      ]);
    }
    return child;
  }
}

/// Function called when a settings switch tile is toggled.
typedef OnSwitch = void Function(bool value);

/// A settings item containing a switch with some description.
///
/// Can be used for all on and off settings.
class SettingsSwitchTile extends SettingsItem {
  static const double maxSwitchWidth = 600.0;

  /// Whether the switch is on or off.
  final bool state;

  final String text;

  /// Function called when user toggles the switch.
  final OnSwitch onSwitch;

  const SettingsSwitchTile(
      {@required double scale, this.state, this.text, this.onSwitch})
      : super(scale);

  @override
  Widget build(BuildContext context) {
    return Container(
        constraints:
            BoxConstraints(minHeight: height, maxWidth: maxSwitchWidth),
        child: SwitchListTile(
          title: SettingsText(text: text, scale: scale),
          value: state,
          onChanged: onSwitch,
        ));
  }
}

/// Simple text based button shown in settings
class SettingsButton extends SettingsItem {
  /// Label the button is displayed with
  final String text;

  /// Action taken when button is pressed
  final VoidCallback onTap;

  /// Constructor.
  const SettingsButton(
      {@required this.text, @required this.onTap, @required double scale})
      : super(scale);

  @override
  Widget build(BuildContext context) {
    return SettingsTile(
      text: text,
      scale: scale,
      onTap: onTap,
    );
  }
}

/// Text with style consistent for the body of a settings page.
class SettingsText extends SettingsItem {
  final String text;

  const SettingsText({@required double scale, this.text}) : super(scale);

  @override
  Widget build(BuildContext context) => _applyStartPadding(
      child: Text(text, style: _textStyle(scale)), scale: scale);
}

/// A tile that can be used to display a setting with icon, text, and optional
/// description.
class SettingsTile extends SettingsItem {
  /// The asset to be displayed
  ///
  /// Only should be set if [iconData] is not
  final String assetUrl;

  /// The icon to be displayed.
  ///
  /// Only should be set if [assetUrl] is not
  final IconData iconData;

  /// The label to display next to the icon
  final String text;

  /// A string displaying the status, errors, or
  /// other secondary text.
  final String description;

  /// Callback to run when the network is tapped
  final VoidCallback onTap;

  /// Constructs a new settings item
  const SettingsTile({
    @required this.text,
    @required double scale,
    this.assetUrl,
    this.iconData,
    this.description,
    this.onTap,
  }) : super(scale);

  @override
  Widget build(BuildContext context) {
    return Container(
        constraints: BoxConstraints(minHeight: height),
        child: ListTile(
            leading: _buildLogo(),
            title: _title(),
            subtitle: description != null ? _subTitle() : null,
            onTap: onTap));
  }

  Widget _buildLogo() {
    if (assetUrl == null && iconData == null) {
      return null; // No logo to show.
    }

    Widget logo = iconData != null
        ? Icon(iconData, size: height, color: Colors.grey[900])
        : Image.asset(
            assetUrl,
            height: height,
            width: height,
          );
    return _applyStartPadding(child: logo, scale: scale);
  }

  Widget _subTitle() => SettingsText(text: description, scale: scale);

  Widget _title() => SettingsText(text: text, scale: scale);
}
