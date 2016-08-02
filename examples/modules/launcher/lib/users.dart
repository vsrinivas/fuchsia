// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';

import 'package:flutter/http.dart' as http;
import 'package:modular_flutter/flutter_module.dart';
import 'package:modular/state_graph.dart';
import 'package:representation_types/person.dart';

const String kUserLabel = 'user';
const String kUsernameLabel = 'username';

typedef void OnImageUrlUpdate();

// Handles writing and reading user data from the graph.
class Users {
  Map<String, String> _usersToAvatarUrls = <String, String>{};
  OnImageUrlUpdate _onImageUrlUpdate;

  Users(this._onImageUrlUpdate);

  // Returns list of other users with whom stories can be shared.
  List<Person> getOtherUsers(StateGraph state) {
    List<Person> people = _getAllUsers(state)
        .map((final SemanticNode user) {
          final String username = user.get(<String>[kUsernameLabel]).value;
          if (_usersToAvatarUrls[username] == null) {
            _loadImageUrl(username);
          }
          return new Person()
            ..name = username.split('@').first
            ..email = username
            ..avatarUrl = _usersToAvatarUrls[username];
        })
        .where(
            (final Person person) => person.email != getCurrentUsername(state))
        .toList();
    return people;
  }

  SemanticNode getUser(StateGraph state, String username) {
    return _getAllUsers(state).firstWhere(
        (final SemanticNode user) =>
            user.get(<String>[kUsernameLabel]).value == username,
        orElse: () => null);
  }

  List<SemanticNode> _getAllUsers(StateGraph state) {
    return state
            ?.getNeighbors(<Uri>[state?.getLabelUrl(kUserLabel)])?.toList() ??
        <SemanticNode>[];
  }

  String getCurrentUsername(StateGraph state) {
    return state.anchors
        .firstWhere(
            (final SemanticNode anchor) =>
                anchor.get(<String>[kUsernameLabel]) != null,
            orElse: () => null)
        ?.get(<String>[kUsernameLabel])?.value;
  }

  // Finds the avatar url from
  // https://picasaweb.google.com/data/entry/api/user/<username>?alt=json
  Future<Null> _loadImageUrl(String username) async {
    final Map<String, String> requestArgs = <String, String>{'alt': 'json'};
    final Uri uri = new Uri.https(
        'picasaweb.google.com', '/data/entry/api/user/$username', requestArgs);
    http.Response response = await http.get(uri);
    if (response.statusCode == 200) {
      dynamic decoded = JSON.decode(response.body);
      _usersToAvatarUrls[username] =
          decoded['entry']['gphoto\$thumbnail']['\$t'];
      _onImageUrlUpdate();
    } else {
      _usersToAvatarUrls[username] = '';
    }
  }
}
