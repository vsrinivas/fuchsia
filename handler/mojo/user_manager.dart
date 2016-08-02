// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:common/mojo_uri_loader.dart';
import 'package:modular_core/uuid.dart';
import 'package:handler/handler.dart';
import 'package:handler/session.dart';
import 'package:modular/builtin_types.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/util/timeline_helper.dart';
import 'package:mojo/application.dart';
import 'package:mojo_services/authentication/authentication.mojom.dart';
import 'package:parser/parser.dart';
import 'package:parser/recipe.dart';

/// A globally unique session which manages all user accounts that have even
/// logged in to Turquoise. This session holds all the necessary user info, like
/// avatar, email, name etc. for every account.
///
/// The schema of this session is
///
/// sessionRoot --> user* -+-> UserInfo
///                        |
///                        |
///                        +--> <Others like shared stories>
///
/// TODO(ksimbili): This is a temporary approach. We need better identity model
/// to identify users and ACLs need to be properly designed so that user can
/// access only the shared space of the other user.
class UserManager {
  static const String userLabel =
      'https://github.com/domokit/modular/wiki/semantic#user';
  static const String usernameLabel =
      'https://github.com/domokit/modular/wiki/semantic#username';
  static const String sessionIdLabel =
      'https://github.com/domokit/modular/wiki/semantic#session-id';
  static String sharedStoryLabel =
      'https://github.com/domokit/modular/wiki/semantic#shared-story';
  static const String defaultRecipeRelPath = '/examples/recipes/launcher.yaml';
  static final Uuid sessionId = new Uuid.zero();

  final Application _application;
  final Graph _graph;
  final Node _rootNode;
  final Handler _handler;
  Recipe _defaultRecipe;
  String _currentUsername;
  final Uri _baseUri;

  UserManager(this._application, this._handler, this._graph, this._rootNode,
      this._baseUri);

  Graph get graph => _graph;

  // Updates the user info in the root session graph of the given user name.
  void _updateUserInfo(Session userRootSession, String username) {
    userRootSession.graph.mutate((GraphMutator mutator) {
      final Node usernameNode = mutator.addNode();
      mutator.addEdge(
          userRootSession.graph.root.id, [usernameLabel], usernameNode.id);
      mutator.setValue(
          usernameNode.id, BuiltinString.label, BuiltinString.write(username));
    });
  }

  /// Updates the recipe for user root session, so that it can pick up any
  /// changes to the default recipe since the last run.
  void _updateRecipe(final Session userRootSession, final Recipe recipe) =>
      userRootSession.metadata.setRecipe(recipe);

  Future<Recipe> _getDefaultRecipe() async {
    if (_defaultRecipe == null) {
      final Uri recipeUri = _baseUri.resolve(defaultRecipeRelPath);
      final MojoUriLoader uriLoader =
          new MojoUriLoader(_application.connectToService);
      final String content = await uriLoader.getString(recipeUri);
      uriLoader.close();
      if (content == null) {
        throw new Exception('Failed to load recipe from: $recipeUri');
      }
      _defaultRecipe = parseRecipe(content);
    }
    return _defaultRecipe;
  }

  Future<String> _getUserName() {
    if (_currentUsername != null) {
      return new Future<String>.value(_currentUsername);
    }

    Completer<String> completer = new Completer<String>();

    final AuthenticationServiceProxy authService =
        new AuthenticationServiceProxy.unbound();
    _application.connectToService("mojo:authentication", authService);
    authService.selectAccount(true, (String username, String error) {
      _currentUsername = username ?? 'anonymous';
      authService.close();
      completer.complete(_currentUsername);
    });
    return completer.future;
  }

  /// Creates the new user node in the graph and also creates the root session
  /// for the user. The user node contains following representation fields for
  /// now.
  ///
  /// UserNode:
  ///     - username
  ///     - sid <-- Root session id.
  Future<Uuid> _createNewUserRootSession(final String username) {
    return traceAsync('$runtimeType _createNewUserRootSession', () async {
      final Session session =
          await _handler.createSession(await _getDefaultRecipe());
      traceSync('$runtimeType _createNewUserRootSession sync', () {
        _graph.mutate((GraphMutator mutator) {
          // Create a new user node.
          final Node userNode = mutator.addNode();
          mutator.addEdge(_rootNode.id, [userLabel], userNode.id);

          // Add session-id as semantic node.
          final Node sessionIdNode = mutator.addNode();
          mutator.addEdge(userNode.id, [sessionIdLabel], sessionIdNode.id);
          _writeString(mutator, sessionIdNode.id, session.id.toBase64());

          // Add username as semantic node.
          final Node usernameNode = mutator.addNode();
          mutator.addEdge(userNode.id, [usernameLabel], usernameNode.id);
          _writeString(mutator, usernameNode.id, username);
        });

        _updateUserInfo(session, username);
      });
      return session.id;
    });
  }

  /// Returns the root session Id for the currently selected user. If the user
  /// is using Turqoise for the first time, this creates a new session for the
  /// user and returns it.
  Future<Uuid> getOrCreateUserRootSessionId() async {
    final String username = await _getUserName();
    final Node userNode = _getUserNode(_rootNode, username);
    if (userNode == null) {
      // We didn't find the user. Must be the new user.
      return _createNewUserRootSession(username);
    }

    final Uuid rootSessionId = Uuid.fromBase64(
        _readString(userNode.singleOutEdgeWithLabels([sessionIdLabel]).target));

    final Future<Session> session = _handler.restoreSession(rootSessionId);
    final Future<Recipe> recipe = _getDefaultRecipe();
    _updateRecipe(await session, await recipe);

    return rootSessionId;
  }

  Node _getUserNode(Node root, String username) {
    final Iterable<Node> allUserNodes = root
        .outEdgesWithLabels([userLabel]).map((final Edge edge) => edge.target);
    return allUserNodes.firstWhere(
        (final Node n) =>
            _readString(n.singleOutEdgeWithLabels([usernameLabel]).target) ==
            username,
        orElse: () => null);
  }

  String _readString(final Node node) =>
      BuiltinString.read(node.getValue(BuiltinString.label));

  void _writeString(
          final GraphMutator mutator, final NodeId id, final String value) =>
      mutator.setValue(id, BuiltinString.label, BuiltinString.write(value));
}
