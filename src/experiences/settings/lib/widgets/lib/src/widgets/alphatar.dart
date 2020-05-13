// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

import 'dart:math';

import 'package:flutter/material.dart';
import 'package:flutter_image/network.dart';
import 'package:meta/meta.dart';

/// Holds all the allowed background colors for an [Alphatar].
///
/// From each material design primary color swatch, the first dark background
/// that needs to be used with white text is chosen.
List<Color> _kAllowedColors = <Color>[
  Colors.red[400],
  Colors.pink[300],
  Colors.purple[300],
  Colors.deepPurple[300],
  Colors.indigo[300],
  Colors.blue[500],
  Colors.lightBlue[600],
  Colors.cyan[700],
  Colors.teal[500],
  Colors.green[600],
  Colors.lightGreen[700],
  Colors.lime[900],
  Colors.orange[800],
  Colors.deepOrange[500],
  Colors.brown[300],
];

/// Alphatar is a 'circle avatar' to represent user profiles
/// If no avatar URL is given for an Alphatar, then the letter of the users name
/// along with a colored circle background will be used.
class Alphatar extends StatelessWidget {
  /// The [Image] to be displayed.
  final Image avatarImage;

  /// The fall-back letter to display when the image is not provided.
  final String letter;

  /// Size of alphatar. Default is 40.0
  final double size;

  /// Color of the letter background.
  final Color backgroundColor;

  /// Creates a new [Alphatar] with the given [Image].
  ///
  /// Either the avatarImage or the letter must be provided.
  Alphatar({
    Key key,
    this.avatarImage,
    this.letter,
    this.size = 40.0,
    Color backgroundColor,
  })  : backgroundColor = backgroundColor ?? _pickRandomColor(),
        assert(avatarImage != null || letter != null),
        super(key: key);

  /// Creates a new [Alphatar] with the given URL.
  ///
  /// Either the avatarUrl or the letter must be provided.
  factory Alphatar.withUrl({
    Key key,
    String avatarUrl,
    String letter,
    double size = 40.0,
    Color backgroundColor,
    bool retry = true,
  }) {
    assert(avatarUrl != null || letter != null);
    assert(retry != null);
    return Alphatar(
      key: key,
      avatarImage: avatarUrl != null && avatarUrl.isNotEmpty
          ? avatarUrl.startsWith('http')
              ? Image(
                  image: retry
                      ? NetworkImageWithRetry(avatarUrl)
                      : NetworkImage(avatarUrl),
                  width: size,
                  height: size,
                  fit: BoxFit.cover,
                )
              : Image.asset(
                  avatarUrl,
                  width: size,
                  height: size,
                  fit: BoxFit.cover,
                )
          : null,
      letter: letter,
      size: size,
      backgroundColor: backgroundColor,
    );
  }

  /// Creates a new [Alphatar] based on the given name.
  factory Alphatar.fromName({
    @required String name,
    Key key,
    Image avatarImage,
    double size = 40.0,
    Color backgroundColor,
  }) {
    assert(name != null);
    return Alphatar(
      key: key,
      avatarImage: avatarImage,
      letter: name.isNotEmpty ? name[0] : '',
      size: size,
      backgroundColor: backgroundColor ?? _pickColorForString(name),
    );
  }

  /// Creates a new [Alphatar] based on the given name and avatar image url.
  factory Alphatar.fromNameAndUrl({
    @required String name,
    @required String avatarUrl,
    Key key,
    double size = 40.0,
    Color backgroundColor,
    bool retry = true,
  }) {
    assert(name != null);
    return Alphatar.withUrl(
      key: key,
      avatarUrl: avatarUrl,
      letter: name.isNotEmpty ? name[0] : '',
      size: size,
      backgroundColor: backgroundColor ?? _pickColorForString(name),
      retry: retry,
    );
  }

  @override
  Widget build(BuildContext context) {
    // If the avatar has a network image, always build a fallback letter
    // underneath so that a placeholder is there while the network call
    // is running.
    //
    // Eventually, a Flutter Image widget should have a fallback widget as
    // part of the implementation, see:
    // https://github.com/flutter/flutter/issues/6229
    Widget image;
    if (avatarImage != null) {
      image = Stack(
        fit: StackFit.passthrough,
        children: <Widget>[
          _buildLetter(),
          avatarImage,
        ],
      );
    } else {
      image = _buildLetter();
    }

    return Container(
      width: size,
      height: size,
      child: ClipOval(
        child: image,
      ),
    );
  }

  Widget _buildLetter() {
    String text = letter?.toUpperCase() ?? '';

    return Container(
      alignment: FractionalOffset.center,
      decoration: BoxDecoration(
        color: text.isNotEmpty ? backgroundColor : Colors.transparent,
        shape: BoxShape.circle,
      ),
      child: text.isNotEmpty
          ? Text(
              text,
              style: TextStyle(
                color: Colors.white,
                fontSize: size / 2.0,
              ),
            )
          : Icon(
              Icons.error,
              size: size,
              color: Colors.red,
            ),
    );
  }

  static Color _pickRandomColor() {
    return _kAllowedColors[Random().nextInt(_kAllowedColors.length)];
  }

  static Color _pickColorForString(String str) {
    return _kAllowedColors[str.hashCode % _kAllowedColors.length];
  }
}
