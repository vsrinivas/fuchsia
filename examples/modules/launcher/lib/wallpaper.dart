// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/widgets.dart';

class Wallpaper extends StatefulWidget {
  final String contextText;
  final TextStyle contextTextStyle;
  final String activityText;
  final TextStyle activityTextStyle;

  Wallpaper(
      {Key key,
      this.contextText,
      this.contextTextStyle,
      this.activityText,
      this.activityTextStyle})
      : super(key: key);
  @override
  WallpaperState createState() => new WallpaperState();
}

class WallpaperState extends State<Wallpaper> {
  final AnimationController _controller =
      new AnimationController(duration: const Duration(milliseconds: 500));
  CurvedAnimation _curve;

  WallpaperState() {
    _curve = new CurvedAnimation(
        parent: _controller,
        curve: Curves.fastOutSlowIn,
        reverseCurve: Curves.fastOutSlowIn.flipped);
  }

  set dimmed(bool dimmed) => setState(() {
        if (dimmed &&
            _controller.status != AnimationStatus.forward &&
            _controller.status != AnimationStatus.completed) {
          _controller.forward();
        } else if (!dimmed &&
            _controller.status != AnimationStatus.reverse &&
            _controller.status != AnimationStatus.dismissed) {
          _controller.reverse();
        }
      });

  @override
  Widget build(_) => new AnimatedBuilder(
      animation: _controller,
      builder: (BuildContext context, Widget child) => new Container(
          foregroundDecoration:
              new BoxDecoration(backgroundColor: _overlayColor),
          child: new Stack(children: <Widget>[
            child,
            new Opacity(
                opacity: 1.0 - _curve.value,
                child: new Container(
                    margin: const EdgeInsets.only(
                        top: 40.0, left: 48.0, right: 48.0),
                    child: new Center(child: new Column(
                        mainAxisSize: MainAxisSize.min,
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        children: <Widget>[
                          new Text(config.contextText.toUpperCase(),
                              style: config.contextTextStyle,
                              textAlign: TextAlign.center),
                          new Container(height: 32.0),
                          new Text(config.activityText,
                              style: config.activityTextStyle,
                              textAlign: TextAlign.center)
                        ]))))
          ])),
      child: new Image(
          image: new AssetImage('lib/res/Wallpaper.jpg'), fit: ImageFit.cover));

  Color get _overlayColor =>
      new Color(((_curve.value * 0xCC).round() << 24) & 0xFF000000);
}
