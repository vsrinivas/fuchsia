// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';

typedef UrlLauncherCallBack = void Function(String url);

/// [RichText] that consists of multiple [TextSpan]s as a result of parsing a
/// string in the markdown format.
///
/// Currently, only supports the link syntax (e.g. [Google](www.google.com))
class MarkdownRichText extends StatelessWidget {
  final String markdown;
  final UrlLauncherCallBack urlLauncher;

  final TextStyle? defaultTextStyle;
  final TextStyle? linkTextStyle;

  const MarkdownRichText(this.markdown,
      {required this.urlLauncher, this.defaultTextStyle, this.linkTextStyle});

  @override
  Widget build(BuildContext context) {
    final defaultStyle =
        defaultTextStyle ?? Theme.of(context).textTheme.bodyText1!;
    final linkStyle = linkTextStyle ??
        defaultStyle.copyWith(color: Theme.of(context).toggleableActiveColor);

    return RichText(
      text: TextSpan(
        style: defaultStyle,
        children: _parseMarkdown(linkStyle: linkStyle),
      ),
    );
  }

  /// Parses the markdown string and turns it into a list of [TextSpan]s.
  ///
  /// The text with a url will be converted to a [TextSpan] with a
  /// [TapGestureRecognizer] and [linkStyle] as its style.
  List<TextSpan> _parseMarkdown({required TextStyle linkStyle}) {
    // The strings in the format of `[string](string)`
    final matches = RegExp(r'\[([^\[])+\]\(.*?\)').allMatches(markdown);

    // If no link pattern is found, return [TextSpan] with the original text
    if (matches.isEmpty) return [TextSpan(text: markdown)];

    String preprocessed = markdown;
    final textSpans = <TextSpan>[];

    for (var m in matches) {
      final linkPattern = m.group(0);
      if (linkPattern == null) continue;

      final split = preprocessed.split(linkPattern);

      // If no string matching with the pattern is found, adds the whole string
      // as the default type of [TextSpan]
      if (split.length < 2) {
        textSpans.add(TextSpan(text: split[0]));
        continue;
      }
      if (split[0].isNotEmpty) {
        textSpans.add(TextSpan(text: split[0]));
      }

      final label = _getLabel(linkPattern);
      final url = _getUrl(linkPattern);

      if (label == null || url == null) {
        textSpans.add(TextSpan(text: linkPattern));
        continue;
      }

      // Adds the label text as a link type of [TextSpan]
      textSpans.add(TextSpan(
        text: label,
        style: linkStyle,
        recognizer: TapGestureRecognizer()
          ..onTap = () {
            urlLauncher(url);
          },
      ));

      // If the number of remaining strings are more than 1, it means that there
      // were more than one strings matching the current pattern.
      // Ignores them in this turn.
      final leftover = split.sublist(1);
      preprocessed =
          leftover.length == 1 ? leftover[0] : leftover.join(linkPattern);
    }

    // Adds the last string as the default type of [TextSpan]
    if (preprocessed.isNotEmpty) {
      textSpans.add(TextSpan(text: preprocessed));
    }

    return textSpans;
  }

  // Extracts text between a pair of square brackets.
  String? _getLabel(String linkPattern) =>
      RegExp(r'(?<=\[).*?(?=\])').firstMatch(linkPattern)?.group(0);

  // Extracts text between a pair of parentheses.
  String? _getUrl(String linkPattern) =>
      RegExp(r'(?<=\]\().*?(?=\))').firstMatch(linkPattern)?.group(0);
}
