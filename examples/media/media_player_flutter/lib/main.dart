// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io' as io;

import 'package:apps.media.lib.flutter/media_player.dart';
import 'package:apps.media.lib.flutter/media_player_controller.dart';
import 'package:apps.media.services/media_metadata.fidl.dart';
import 'package:apps.media.services/problem.fidl.dart';
import 'package:application.lib.app.dart/app.dart';

import 'package:flutter/widgets.dart';
import 'package:flutter/material.dart';

import 'asset.dart';
import 'config.dart';

final ApplicationContext _appContext = new ApplicationContext.fromStartupInfo();
final MediaPlayerController _controller =
    new MediaPlayerController(_appContext.environmentServices);

const List<String> _configFileNames = const <String>[
  '/data/media_player_flutter.config',
  '/system/data/media_player_flutter/media_player_flutter.config',
];

Asset _assetToPlay;
Asset _leafAssetToPlay;
int _playlistIndex;

/// Plays the specified asset.
void _play(Asset asset) {
  assert(asset != null);

  _assetToPlay = asset;
  _playlistIndex = 0;

  if (_assetToPlay.children != null) {
    assert(_assetToPlay.children.isNotEmpty);
    _playLeafAsset(_assetToPlay.children[0]);
  } else {
    _playLeafAsset(_assetToPlay);
  }
}

/// If [_assetToPlay] is a playlist and hasn't been played through, this method
/// plays the next asset in [_assetToPlay] and returns true. Returns false
/// otherwise.
bool _playNext() {
  if (_assetToPlay == null ||
      _assetToPlay.children == null ||
      _assetToPlay.children.length <= ++_playlistIndex) {
    return false;
  }

  _playLeafAsset(_assetToPlay.children[_playlistIndex]);

  return true;
}

void _playLeafAsset(Asset asset ) {
  assert(asset.type != AssetType.playlist);

  _leafAssetToPlay = asset;

  if (_leafAssetToPlay.type == AssetType.remote) {
    _controller.connectToRemote(
      device: _leafAssetToPlay.device,
      service: _leafAssetToPlay.service,
    );
  } else {
    _controller.open(_leafAssetToPlay.uri);
    _controller.play();
  }
}

/// Screen for asset playback.
class _PlaybackScreen extends StatefulWidget {
  _PlaybackScreen({Key key}) : super(key: key);

  @override
  _PlaybackScreenState createState() => new _PlaybackScreenState();
}

class _PlaybackScreenState extends State<_PlaybackScreen> {
  @override
  void initState() {
    _controller.addListener(_handleControllerChanged);
    super.initState();
  }

  @override
  void dispose() {
    _controller.removeListener(_handleControllerChanged);
    super.dispose();
  }

  /// Handles change notifications from the controller.
  void _handleControllerChanged() {
    setState(() {
      if (_controller.ended) {
        _playNext();
      }
    });
  }

  /// Adds a label to list [to] if [label] isn't null.
  void _addLabel(String label, Color color, double fontSize, List<Widget> to) {
    if (label == null) {
      return;
    }

    to.add(new Container(
      margin: const EdgeInsets.only(left: 10.0),
      child: new Text(
        label,
        style: new TextStyle(color: color, fontSize: fontSize),
      ),
    ));
  }

  /// Adds a problem description to list [to] if there is a problem.
  void _addProblem(List<Widget> to) {
    Problem problem = _controller.problem;
    if (problem != null) {
      String text;

      if (problem.details != null && problem.details.isNotEmpty) {
        text = problem.details;
      } else {
        switch (problem.type) {
          case Problem.kProblemInternal:
            text = 'Internal error';
            break;
          case Problem.kProblemAssetNotFound:
            text = 'The requested content was not found';
            break;
          case Problem.kProblemMediaTypeNotSupported:
            text = 'The requested content is in an unsupported format';
            break;
          default:
            text = 'Unrecognized problem type ${problem.type}';
            break;
        }
      }

      _addLabel(text, Colors.white, 20.0, to);
      _addLabel(_leafAssetToPlay.toString(), Colors.grey[800], 15.0, to);
    }
  }

  @override
  Widget build(BuildContext context) {
    List<Widget> columnChildren = new List<Widget>();

    columnChildren.add(new MediaPlayer(_controller));

    MediaMetadata metadata = _controller.metadata;
    if (metadata != null) {
      _addLabel(metadata.title ?? _leafAssetToPlay.title ?? '(untitled)',
          Colors.white, 20.0, columnChildren);
      _addLabel(metadata.artist ?? _leafAssetToPlay.artist, Colors.grey[600],
          15.0, columnChildren);
      _addLabel(metadata.album ?? _leafAssetToPlay.album, Colors.grey[800],
          15.0, columnChildren);
    }

    _addProblem(columnChildren);

    return new Material(
      color: Colors.black,
      child: new Stack(
        children: <Widget>[
          new Positioned(
            left: 0.0,
            right: 0.0,
            top: 0.0,
            child: new Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: columnChildren,
            ),
          ),
          new Positioned(
            right: 0.0,
            top: 0.0,
            child: new IconButton(
              icon: new Icon(Icons.arrow_back),
              size: 60.0,
              onPressed: () {
                _controller.close();
                Navigator.of(context).pop();
              },
              color: Colors.white,
            ),
          ),
        ],
      ),
    );
  }
}

/// Screen for asset selection
class _ChooserScreen extends StatefulWidget {
  _ChooserScreen({Key key}) : super(key: key);

  @override
  _ChooserScreenState createState() => new _ChooserScreenState();
}

class _ChooserScreenState extends State<_ChooserScreen> {
  List<Asset> _assets = <Asset>[];

  @override
  void initState() {
    _readConfig();
    super.initState();
  }

  Future<Null> _readConfig() async {
    for (String fileName in _configFileNames) {
      try {
        List<Asset> assets = await readConfig(fileName);
        if (!mounted) {
          return;
        }

        setState(() {
          _assets = assets;
        });

        return;
      } on ArgumentError {
        // File doesn't exist. Continue.
      } on FormatException catch (e) {
        print('Failed to parse config $fileName: $e');
        io.exit(0);
        return;
      }
    }

    print('No config file found');
    io.exit(0);
  }

  Widget _buildChooseButton(Asset asset) {
    IconData iconData;

    switch (asset.type) {
      case AssetType.movie:
        iconData = Icons.movie;
        break;
      case AssetType.song:
        iconData = Icons.music_note;
        break;
      case AssetType.playlist:
        iconData = Icons.playlist_play;
        break;
      case AssetType.remote:
        iconData = Icons.settings_remote;
        break;
    }

    return new RaisedButton(
      onPressed: () {
        _play(asset);
        Navigator.of(context).pushNamed('/play');
      },
      color: Colors.black,
      child: new Row(
        children: <Widget>[
          new Icon(
            iconData,
            size: 60.0,
            color: Colors.grey[200],
          ),
          new Column(
            mainAxisAlignment: MainAxisAlignment.center,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: <Widget>[
              new Text(
                asset.title ?? '(no title)',
                style: new TextStyle(color: Colors.grey[200], fontSize: 18.0),
              ),
              new Text(
                asset.artist ?? '',
                style: new TextStyle(color: Colors.grey[600], fontSize: 13.0),
              ),
              new Text(
                asset.album ?? '',
                style: new TextStyle(color: Colors.grey[800], fontSize: 13.0),
              ),
            ],
          ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return new Material(
      color: Colors.black,
      child: new Stack(
        children: <Widget>[
          new ListView(
            itemExtent: 75.0,
            children: _assets.map(_buildChooseButton).toList(),
          ),
          new Positioned(
            right: 0.0,
            top: 0.0,
            child: new IconButton(
              icon: new Icon(Icons.close),
              size: 60.0,
              onPressed: () {
                io.exit(0);
              },
              color: Colors.white,
            ),
          ),
        ],
      ),
    );
  }
}

void main() {
  runApp(new MaterialApp(
    title: 'Media Player',
    home: new _ChooserScreen(),
    routes: <String, WidgetBuilder>{
      '/play': (BuildContext context) => new _PlaybackScreen()
    },
    theme: new ThemeData(primarySwatch: Colors.blue),
    debugShowCheckedModeBanner: false,
  ));
}
