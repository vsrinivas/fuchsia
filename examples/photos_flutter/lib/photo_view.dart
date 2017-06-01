// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';

import 'photo.dart';

class PhotoView extends StatelessWidget {
  final Photo _photo;

  PhotoView({Key key, Photo photo})
      : this._photo = photo,
        super(key: key);

  @override
  Widget build(BuildContext context) {
    return new Scaffold(
      appBar: new AppBar(title: new Text(_photo.description)),
      body: new Center(
        child: new Image(image: _photo.imageProvider),
      ),
    );
  }
}
