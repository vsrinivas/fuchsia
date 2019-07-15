// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:collection';

import 'package:analyzer/dart/ast/ast.dart';
import 'package:analyzer/dart/ast/visitor.dart';

// DetectChangesVisitor will visit all nodes in the AST recursively, starting
// from the root node (Dart file) down. This class generates a JSON object by
// creating a Map object at each node, and adding keys / values / children by
// maintaining a stack during traversal.
//
// Most of the work is done in the visit method.
//
// SplayTreeMap is used throughout to maintain a consistent order of map keys in
// the final JSON object.

class DetectChangesVisitor<R> extends RecursiveAstVisitor<R> {
  ListQueue<SplayTreeMap<String, dynamic>> stack;
  SplayTreeMap<String, dynamic> file;
  String defaultVarValue;

  DetectChangesVisitor(String filename) {
    file = SplayTreeMap();
    file['name'] = filename;
    file['type'] = 'file';

    stack = ListQueue()..addFirst(file);
  }

  @override
  R visitExportDirective(ExportDirective node) {
    return visit(
        node, '${node.uri.stringValue}', SplayTreeMap<String, dynamic>());
  }

  @override
  R visitClassDeclaration(ClassDeclaration node) {
    return visit(node, '${node.name.name}', SplayTreeMap<String, dynamic>());
  }

  @override
  R visitConstructorDeclaration(ConstructorDeclaration node) {
    String name = stack.first['name'];
    if (node.name != null) {
      name = '$name.${node.name}';
    }
    return visit(node, name, SplayTreeMap<String, dynamic>());
  }

  @override
  R visitMethodDeclaration(MethodDeclaration node) {
    SplayTreeMap<String, dynamic> map = SplayTreeMap();

    String name = '${node.name}';
    map['returnType'] = '${node.returnType}';
    map['isAbstract'] = node.isAbstract;
    map['isOperator'] = node.isOperator;
    map['isStatic'] = node.isStatic;
    map['isGetter'] = node.isGetter;
    map['isSetter'] = node.isSetter;

    return visit(node, name, map);
  }

  @override
  R visitFunctionDeclaration(FunctionDeclaration node) {
    if (stack.first['type'] == 'file') {
      SplayTreeMap<String, dynamic> map = SplayTreeMap();

      String name = '${node.name}';
      map['returnType'] = '${node.returnType}';
      map['isGetter'] = node.isGetter;
      map['isSetter'] = node.isSetter;

      return visit(node, name, map);
    } else {
      return super.visitFunctionDeclaration(node);
    }
  }

  @override
  R visitEnumDeclaration(EnumDeclaration node) {
    String name = '${node.declaredElement.toString()}';
    if (name.startsWith('enum')) {
      name = name.split(' ')[1];
    }
    return visit(node, name, SplayTreeMap<String, dynamic>());
  }

  @override
  R visitEnumConstantDeclaration(EnumConstantDeclaration node) {
    return visit(node, '${node.name}', SplayTreeMap<String, dynamic>());
  }

  @override
  R visitExtendsClause(ExtendsClause node) {
    SplayTreeMap<String, dynamic> map = SplayTreeMap();
    map['name'] = node.superclass.name.name;
    map['type'] = 'extends';
    stack.first['ExtendsClause'] = map;
    node.visitChildren(this);
    return null;
  }

  @override
  R visitImplementsClause(ImplementsClause node) {
    String type = 'ImplementsClause';
    if (!stack.first.containsKey(type)) {
      stack.first[type] = SplayTreeMap<String, dynamic>();
    }

    for (var interfaceType in node.interfaces) {
      String name = interfaceType.name.name;
      SplayTreeMap<String, dynamic> map = SplayTreeMap();
      map['name'] = name;
      map['type'] = 'interface';
      stack.first[type][name] = map;
    }

    node.visitChildren(this);
    return null;
  }

  @override
  R visitWithClause(WithClause node) {
    String type = 'WithClause';
    if (!stack.first.containsKey(type)) {
      stack.first[type] = SplayTreeMap<String, dynamic>();
    }

    for (var mixinType in node.mixinTypes) {
      String name = mixinType.name.name;
      SplayTreeMap<String, dynamic> map = SplayTreeMap();
      map['name'] = name;
      map['type'] = 'mixin';
      stack.first[type][name] = map;
    }

    node.visitChildren(this);
    return null;
  }

  @override
  R visitSimpleFormalParameter(SimpleFormalParameter node) {
    if (stack.first['type'] == 'FunctionDeclarationImpl' ||
        stack.first['type'] == 'MethodDeclarationImpl' ||
        stack.first['type'] == 'ConstructorDeclarationImpl') {
      String name = node.identifier.name;
      SplayTreeMap<String, dynamic> map = SplayTreeMap();
      map['isOptional'] = node.isOptional;
      map['isOptionalNamed'] = node.isOptionalNamed;
      map['isOptionalPositional'] = node.isOptionalPositional;
      map['isPositional'] = node.isPositional;
      map['isRequired'] = node.isRequired;
      map['isRequiredNamed'] = node.isRequiredNamed;
      map['isRequiredPositional'] = node.isRequiredPositional;
      map['isConst'] = node.isConst;
      map['isFinal'] = node.isFinal;
      map['varType'] = '${node.type}';
      if (node.covariantKeyword != null) {
        map['covariant'] = '${node.covariantKeyword}';
      }
      return visit(node, name, map);
    } else {
      return super.visitSimpleFormalParameter(node);
    }
  }

  @override
  R visitTypeParameter(TypeParameter node) {
    return visit(node, '$node', SplayTreeMap<String, dynamic>());
  }

  @override
  R visitTypeParameterList(TypeParameterList node) {
    return visit(node, '$node', SplayTreeMap<String, dynamic>());
  }

  @override
  R visitFieldFormalParameter(FieldFormalParameter node) {
    return visit(node, '$node', SplayTreeMap<String, dynamic>());
  }

  @override
  R visitVariableDeclaration(VariableDeclaration node) {
    if (stack.first['type'] == 'VariableDeclarationListImpl' ||
        stack.first['type'] == 'file') {
      SplayTreeMap<String, dynamic> map = SplayTreeMap();

      map['isConst'] = node.isConst;
      map['isFinal'] = node.isFinal;
      map['isLate'] = node.isLate;

      return visit(node, '${node.name.name}', map);
    } else {
      return super.visitVariableDeclaration(node);
    }
  }

  @override
  R visitVariableDeclarationList(VariableDeclarationList node) {
    // Ignore variable definitions inside methods and functions.
    if (stack.first['type'] == 'ClassDeclarationImpl' ||
        stack.first['type'] == 'file') {
      // VariableDeclarationList defines a variable type and a list of
      // variables, but it does not define a unique "name". As such, it is
      // likely that when encoding the AST into JSON, they will collide with
      // other VariableDeclarationList entries in the same SplayTreeMap.
      //
      // To avoid this, pass "skip = true" to the visit method. This will
      // remove "VariableDeclarationList" from the JSON map, and move all of
      // its children to be children of its parent.
      var result =
          visit(node, '$node', SplayTreeMap<String, dynamic>(), skip: true);

      if (stack.first.containsKey('VariableDeclarationImpl')) {
        for (var key in stack.first['VariableDeclarationImpl'].keys) {
          var thisVar = stack.first['VariableDeclarationImpl'][key];
          if (!thisVar.containsKey('varType')) {
            thisVar['varType'] = node.type.toString();
          }
        }
      }

      return result;
    } else {
      return super.visitVariableDeclarationList(node);
    }
  }

  // This method visits each AST node by doing the following:
  //
  // 1) Create a new map object and store it in the stack.
  // 2) Create a reference from the parent map to the new map.
  // 3) Visit all children of this node recursively.
  // 4) Remove this map object from the stack.
  //
  // If "skip" is set to true, skip step 2 (don't create a link).
  // This is required for cases where we don't want a given AST node to be
  // represented in the final JSON object.
  R visit(AstNode node, String name, SplayTreeMap<String, dynamic> map,
      {bool skip = false}) {
    String type = '${node.runtimeType}';
    map['name'] = name;
    map['type'] = type;

    // If we intend to skip adding the given object to the JSON map, skip adding
    // the link in the parent map here.
    if (!skip && !Identifier.isPrivateName(name)) {
      if (!stack.first.containsKey(type)) {
        stack.first[type] = SplayTreeMap<String, dynamic>();
      }
      stack.first[type][name] = map;
    }

    stack.addFirst(map);
    node.visitChildren(this);

    if (skip) {
      // Add all children to be children of the parent map.
      var second = stack.elementAt(1);
      for (var key in map.keys) {
        if (key != 'name' && key != 'type') {
          if (second.containsKey(key)) {
            second[key].addAll(map[key]);
          } else {
            second[key] = map[key];
          }
        }
      }
    }

    stack.removeFirst();
    return null;
  }
}
