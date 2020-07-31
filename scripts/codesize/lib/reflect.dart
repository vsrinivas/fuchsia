// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:core';
import 'dart:core' as core;
import 'dart:mirrors';

import 'queries/index.dart';

bool _parseBool(String val) {
  if (val == 'true') return true;
  if (val == 'false') return false;
  throw Exception('$val is not a valid value for boolean');
}

class ReflectQuery {
  static String nameType(Type type) {
    final classMirror = reflectClass(type);
    return Query.stripQuerySuffix(classMirror.simpleName.toString());
  }

  static Query instantiate(QueryFactory f, Map<String, String> arguments) {
    final classMirror = reflectClass(f.type);
    final constructors = List<MethodMirror>.from(classMirror.declarations.values
        .whereType<MethodMirror>()
        .where((decl) => decl.isConstructor));
    if (arguments.isNotEmpty && constructors.isEmpty) {
      throw Exception(
          '$f has no constructors, but specified arguments $arguments');
    }
    for (final constructor in constructors) {
      if (constructor is MethodMirror) {
        final List<ParameterMirror> parameters = constructor.parameters;
        if (parameters.any((param) => !param.isNamed)) {
          throw Exception('$constructor must only use named parameters');
        }
        final parameterNames =
            parameters.where((param) => param.isNamed).map((e) => e.simpleName);
        if (arguments.keys.any((arg) => !parameterNames.contains(Symbol(arg))))
          continue;
        final resolvedArgs =
            Map<Symbol, dynamic>.fromEntries(arguments.entries.map((entry) {
          final key = entry.key, value = entry.value;
          final param =
              parameters.firstWhere((param) => param.simpleName == Symbol(key));
          final name = param.simpleName;
          switch (param.type.simpleName) {
            case #int:
              return MapEntry<Symbol, dynamic>(name, int.parse(value));
            case #bool:
              return MapEntry<Symbol, dynamic>(name, _parseBool(value));
            case #String:
              return MapEntry<Symbol, dynamic>(name, value);
            default:
              throw Exception('Unexpected type ${param.type.simpleName}');
          }
        }));
        return classMirror
            .newInstance(core.Symbol.empty, [], resolvedArgs)
            .reflectee;
      }
    }
    final constructorDesc = constructors
        .map((c) => describeConstructor(c, f))
        .map((s) => ' - tried $s')
        .join('\n');
    throw Exception(
        'Did not find suitable constructor for ${f.type} with $arguments.\n\n'
        '$constructorDesc\n');
  }

  /// Prints the method signature for `constructor`.
  static String describeConstructor(MethodMirror constructor, QueryFactory f) {
    final params = describeArgDeclarations(constructor);
    if (params.isEmpty) {
      return '${f.name}()';
    } else {
      return '${f.name}({${params.join(", ")}})';
    }
  }

  static Iterable<String> describeArgDeclarations(MethodMirror constructor) =>
      constructor.parameters.map((param) {
        final typeName = MirrorSystem.getName(param.type.simpleName);
        final paramName = MirrorSystem.getName(param.simpleName);
        var str = '$typeName $paramName';
        if (param.hasDefaultValue) {
          str += ' = ${param.defaultValue.reflectee}';
        }
        return str;
      });

  static Iterable<String> describeQueryConstructors(QueryFactory f) {
    final classMirror = reflectClass(f.type);
    final constructors = List<MethodMirror>.from(classMirror.declarations.values
        .whereType<MethodMirror>()
        .where((decl) => decl.isConstructor));
    return constructors.map((c) => describeConstructor(c, f));
  }

  static bool hasCustomArguments(QueryFactory f) {
    final classMirror = reflectClass(f.type);
    final constructors = List<MethodMirror>.from(classMirror.declarations.values
        .whereType<MethodMirror>()
        .where((decl) => decl.isConstructor));
    return constructors.any((c) => c.parameters.isNotEmpty);
  }
}
