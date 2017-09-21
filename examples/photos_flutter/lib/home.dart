// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:meta/meta.dart';

import 'photo.dart';
import 'photo_storage.dart';
import 'photo_view.dart';

void _log(String msg) {
  print('[Photos Flutter Example] $msg');
}

@immutable
class Home extends StatefulWidget {
  final PhotoStorage _storage;

  Home({Key key, PhotoStorage storage})
      : _storage = storage,
        super(key: key);

  @override
  _HomeState createState() => new _HomeState(_storage);
}

class _HomeState extends State<Home> {
  final String _imageUrl = "http://lorempixel.com/400/200/cats/";
  PhotoStorage _storage;

  _HomeState(this._storage);

  Widget _buildGrid() {
    final List<Photo> photos = _storage.photos;
    if (photos.isEmpty) {
      return new Text("Click on \"+\" to add photos.");
    }
    return new GridView.count(
        crossAxisCount: 3,
        childAspectRatio: 1.0,
        padding: const EdgeInsets.all(4.0),
        mainAxisSpacing: 4.0,
        crossAxisSpacing: 4.0,
        children: photos.map((Photo photo) {
          return new GridTile(
              footer: new GridTileBar(title: new Text(photo.description)),
              child: new GestureDetector(
                  onTap: () => _showPhoto(context, photo),
                  child: new Image(image: photo.imageProvider)));
        }).toList());
  }

  void _showPhoto(BuildContext context, Photo photo) {
    _log("clicked on photo: " + photo.description);
    Navigator.of(context).push(new MaterialPageRoute<Null>(
        builder: (BuildContext context) => new PhotoView(photo: photo)));
  }

  void _onPlusPressed() {
    setState(() {
      _log("clicked on \"+\"");
      int id = _storage.photos.length;
      _storage.add(new Photo(_storage, "$_imageUrl/?$id", "cat #$id",
          new DateTime.now().millisecondsSinceEpoch));
    });
  }

  @override
  Widget build(BuildContext context) {
    Widget child = new Center(
        child: new Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16.0),
            child: _buildGrid()));

    return new Scaffold(
      appBar: new AppBar(title: new Text('Photos Example')),
      body: child,
      floatingActionButton: new FloatingActionButton(
          child: new Icon(Icons.add), onPressed: _onPlusPressed),
    );
  }
}
