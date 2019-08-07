import 'package:flutter/material.dart';

/// Defines a class to hold styling information for Ermine UX.
class ErmineStyle {
  static final ErmineStyle instance = ErmineStyle._internal();

  factory ErmineStyle() => instance;

  ErmineStyle._internal();

  /// Theme used across Ermine UX.
  static ThemeData kErmineTheme = ThemeData(
    brightness: Brightness.dark,
    fontFamily: 'Roboto Mono',
    textTheme: TextTheme(),
  );

  /// Background color.
  static Color kBackgroundColor = Colors.black;

  /// Story title color.
  static Color kStoryTitleColor = Colors.white;

  /// Story title background color.
  static Color kStoryTitleBackgroundColor = kBackgroundColor;

  /// Story title height.
  static double kStoryTitleHeight = 24;

  /// Story border width.
  static double kBorderWidth = 0;

  /// Screen animation duration in milliseconds. Applies to story fullscreen
  /// transitions, topbar and overview.
  static Duration kScreenAnimationDuration = Duration(milliseconds: 550);

  /// Screen animation curve.
  static Curve kScreenAnimationCurve = Curves.easeOutExpo;

  /// Duration used for items in Ask suggestion list animation.
  static Duration kAskItemAnimationDuration = Duration(milliseconds: 100);

  /// Curve used for items in Ask suggestion list animation.
  static Curve kAskItemAnimationCurve = Curves.easeOutExpo;
}
