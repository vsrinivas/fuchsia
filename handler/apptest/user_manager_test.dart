// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:modular_core/uuid.dart';
import 'package:handler/graph/session_graph.dart';
import 'package:handler/handler.dart';
import 'package:handler/session_metadata.dart';
import 'package:modular/builtin_types.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:mojo/application.dart';
import 'package:test/test.dart';

import '../mojo/user_manager.dart';

List<Node> _getUserNodes(final SessionGraph graph) {
  return graph.root.outEdges
      .where((Edge edge) => edge.labels.contains(UserManager.userLabel))
      .map((Edge edge) => edge.target)
      .toList();
}

String _readString(Node node) =>
    BuiltinString.read(node.getValue(BuiltinString.label));

void testUserManager(final Application app, final String url) {
  Handler handler;
  SessionGraph graph;
  UserManager userManager;
  setUp(() async {
    handler = new Handler();

    graph = await handler.graphStore.createGraph(UserManager.sessionId);
    userManager =
        new UserManager(app, handler, graph, graph.root, Uri.parse(url));
  });

  void testRecipe(final SessionGraph graph, {final bool testForExist: true}) {
    final SessionMetadata metadata =
        new SessionMetadata(graph, graph.metadataNode);
    expect(metadata.getRecipe() != null, equals(testForExist));
  }

  Future<Uuid> testCreateUser() async {
    final Uuid sessionId = await userManager.getOrCreateUserRootSessionId();

    final List<Node> userNodes = _getUserNodes(graph);
    expect(userNodes.length, equals(1));

    // There are some data written to user_manager graph, and some data written
    // to user root session graph. We should verify both.

    // Verify 'username' node in user_manager graph.
    final Node usernameNode = userNodes[0]
        .singleOutEdgeWithLabels([UserManager.usernameLabel]).target;
    expect(_readString(usernameNode), isNotEmpty);

    // Verify 'session-id' node in user_manager graph.
    final Node rootSessionIdNode = userNodes[0]
        .singleOutEdgeWithLabels([UserManager.sessionIdLabel]).target;
    expect(_readString(rootSessionIdNode), equals(sessionId.toBase64()));

    // Verify 'username', 'recipe' updated are updated in the user root session.
    final SessionGraph userRootSessionGraph =
        await handler.graphStore.findGraph(sessionId);
    expect(
        _readString(userRootSessionGraph.root
            .singleOutEdgeWithLabels([UserManager.usernameLabel]).target),
        equals(_readString(usernameNode)));
    testRecipe(userRootSessionGraph, testForExist: true);

    return sessionId;
  }

  group('User manager', () {
    test('Create root session for new User', () async {
      expect(graph.edges.length, equals(1)); // session metadata
      expect(_getUserNodes(graph).length, equals(0));
      await testCreateUser();
    });

    test('Return root session for old user', () async {
      expect(graph.edges.length, equals(1)); // session metadata
      expect(_getUserNodes(graph).length, equals(0));

      final Uuid createdSessionId = await testCreateUser();

      final Uuid returnedSessionId = await testCreateUser();

      expect(returnedSessionId, equals(createdSessionId));
    });

    test('Update recipe of user root session', () async {
      expect(graph.edges.length, equals(1)); // session metadata
      expect(_getUserNodes(graph).length, equals(0));

      final Uuid createdSessionId = await testCreateUser();

      // Delete the recipe from metadata node.
      final SessionGraph userRootSessionGraph =
          await handler.graphStore.findGraph(createdSessionId);
      userRootSessionGraph.mutate((final GraphMutator mutator) {
        mutator.setValue(
            userRootSessionGraph.metadataNode.id, 'internal:recipe', null);
      });
      testRecipe(userRootSessionGraph, testForExist: false);

      // Verify that recipe exists for the returned user. This recipe will be
      // the updated recipe.
      final Uuid returnedSessionId = await testCreateUser();

      expect(returnedSessionId, equals(createdSessionId));
    });
  });
}
