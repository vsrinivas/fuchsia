// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter_test/flutter_test.dart';
import 'package:mondrian/models/tree/tree.dart';

/// Convenience function for making trees

Tree<String> t(Tree<String> node, {List<Tree<String>> children}) {
  children?.forEach(node.add);
  return node;
}

void main() {
  Tree<String> grandparent; // the root
  Tree<String> parent;
  Tree<String> uncle;
  Tree<String> aunt;
  Tree<String> cousin;
  Tree<String> sibling;
  Tree<String> sibling2;
  Tree<String> niece;
  Tree<String> child;

  Tree<String> trimmedTree;
  Forest<String> forest;

  Tree<String> secondRoot;
  Tree<String> friend;
  Tree<String> childFriend;

  List<Tree<String>> expectedTree;
  List<Tree<String>> expectedForest;

  setUp(() {
    grandparent = Tree<String>(value: 'grandparent');
    parent = Tree<String>(value: 'parent');
    uncle = Tree<String>(value: 'uncle');
    aunt = Tree<String>(value: 'aunt');
    cousin = Tree<String>(value: 'cousin');
    sibling = Tree<String>(value: 'sibling');
    sibling2 = Tree<String>(value: 'sibling2');
    niece = Tree<String>(value: 'niece');
    child = Tree<String>(value: 'child');

    grandparent = t(
      // the root
      grandparent,
      children: <Tree<String>>[
        t(
          parent,
          children: <Tree<String>>[
            sibling,
            child,
            t(
              sibling2,
              children: <Tree<String>>[niece],
            ),
          ],
        ),
        uncle,
        t(
          aunt,
          children: <Tree<String>>[cousin],
        ),
      ],
    );

    trimmedTree = t(
      Tree<String>(value: grandparent.value),
      children: <Tree<String>>[uncle, aunt],
    );

    forest = Forest<String>()..add(grandparent);

    secondRoot = Tree<String>(value: 'parent of friend');
    friend = Tree<String>(value: 'friend');
    childFriend = Tree<String>(value: 'child of friend');

    secondRoot = t(
      secondRoot, // the root
      children: <Tree<String>>[
        t(
          friend,
          children: <Tree<String>>[childFriend],
        ),
      ],
    );

    expectedTree = <Tree<String>>[
      grandparent,
      parent,
      uncle,
      aunt,
      sibling,
      child,
      sibling2,
      cousin,
      niece
    ];

    expectedForest = <Tree<String>>[
      grandparent,
      parent,
      uncle,
      aunt,
      sibling,
      child,
      sibling2,
      cousin,
      niece,
      secondRoot,
      friend,
      childFriend
    ];

    forest.add(secondRoot);
  });

  group('Test Tree in tree.dart', () {
    test('we can find root', () {
      expect(niece.root.value, equals(grandparent.value));
    });
    test('we can find correct children', () {
      print(grandparent.children);
      expect(grandparent.children, equals(<Tree<String>>[parent, uncle, aunt]));
    });
    test('we can find correct ancestors', () {
      expect(niece.ancestors,
          equals(<Tree<String>>[sibling2, parent, grandparent]));
    });
    test('we can find correct parent', () {
      expect(niece.parent, equals(sibling2));
    });
    test('flatten works as expected (breadth-first)', () {
      // flatten - breadth first
      expect(grandparent.flatten().toList(), equals(expectedTree));
    });
    test('we detach nodes correctly', () {
      parent.detach();
      expect(grandparent.values, equals(trimmedTree.values));
    });
    test('ancestors are correct post-detachement', () {
      parent.detach();
      expect(niece.ancestors, equals(<Tree<String>>[sibling2, parent]));
    });
    test('siblings are found correctly', () {
      expect(child.siblings, equals(<Tree<String>>[sibling, sibling2]));
    });
    test('we can use find tp find subsequent nodes', () {
      expect(grandparent.find('niece'), equals(niece));
    });
    test('find does not search upwards in tree', () {
      expect(niece.find('root'), equals(null));
    });
  });

  group('Test Forest in tree.dart', () {
    test('Forest roots found correctly', () {
      expect(forest.roots, equals(<Tree<String>>[grandparent, secondRoot]));
    });
    test('Adding root to Forest', () {
      forest.add(secondRoot);
      expect(forest.roots,
          equals(<Tree<String>>[grandparent, secondRoot, secondRoot]));
    });
    test('Flatten forest is breadth-first', () {
      expect(forest.flatten().toList(), equals(expectedForest));
    });
    test('Forest values', () {
      List<String> evals =
          expectedForest.map((Tree<String> f) => f.value).toList();
      expect(forest.values.toList(), equals(evals));
    });
    test('Remove node from Forest, roots unchanged', () {
      forest.remove(parent);
      expect(forest.roots, equals(<Tree<String>>[grandparent, secondRoot]));
    });
    test('Remove node from Forest, node really removed', () {
      forest.remove(parent);
      expect(forest.find('grandparent').find('parent'), equals(null));
    });
    test('Remove root from Forest', () {
      // remove a root
      forest.remove(grandparent);
      // new roots
      expect(forest.roots,
          equals(<Tree<String>>[secondRoot, parent, uncle, aunt]));
    });
    test('Find a node in Forest', () {
      expect(forest.find('sibling'), equals(sibling));
    });
    test('mapForest to Uppercase', () {
      Forest<String> newForest =
          forest.mapForest((String f) => f.toUpperCase());
      expect(newForest.find('NIECE').parent.parent.parent,
          equals(newForest.find('GRANDPARENT')));
    });
  });
}
