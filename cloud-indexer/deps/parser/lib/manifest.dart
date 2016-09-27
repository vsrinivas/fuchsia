// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:developer';

import 'package:collection/collection.dart';
import 'package:modular_core/entity/schema.dart';

import 'expression.dart';
import 'parser.dart' show parseManifest;
import 'recipe.dart' show Step;

class Manifest {
  final String title;
  final Uri url;
  final Uri icon;
  final int themeColor;
  final Uses use;
  final Verb verb;
  final List<PathExpr> input;
  final List<PathExpr> output;
  final List<PathExpr> display;
  final List<PathExpr> compose;

  final List<Schema> schemas;

  final String arch;
  final String modularRevision;

  /// The filename or URL this Manifest was loaded from.
  String src;

  Manifest(this.title, this.url, this.use, this.verb, this.input, this.output,
      this.display, this.compose,
      {this.icon, this.themeColor, this.schemas, this.arch, this.modularRevision});

  /// Creates a dummy manifest from a Step that would match this Step. Used by
  /// tests and simulator.
  factory Manifest.fromStep(final Step step) {
    return new Manifest(
        'dummy',
        Uri.parse('http://tq.io/dummy/${step.verb}'),
        new Uses(),
        step.verb,
        _manifestExpr(step.input),
        _manifestExpr(step.output),
        _manifestExpr(step.display),
        _manifestExpr(step.compose));
  }

  /// Parses a YAML [String] representing a manifest into a [Manifest].
  /// Throws a [ParserError] if an error occurs.
  factory Manifest.parseYamlString(String yaml) {
    return Timeline.timeSync("Manifest.parseYamlString", () {
      return parseManifest(yaml);
    });
  }

  factory Manifest.fromJsonString(String jsonString) {
    // TODO(thatguy): This process lacks adequate validation and error handling
    // when a malformed [jsonString] is given.
    return Timeline.timeSync("Manifest.fromJsonString",
        () => new Manifest.fromJson(JSON.decode(jsonString)));
  }

  factory Manifest.fromJson(Map<String, dynamic> values) {
    return Timeline.timeSync("Manifest.fromJson", () {
      List<Schema> schemas = [];
      if (values['schemas'] != null) {
        schemas = values['schemas'].map((i) => new Schema.fromJson(i)).toList();
      }
      return new Manifest(
          values['title'],
          Uri.parse(values['url']),
          new Uses.fromJson(values['use']),
          new Verb.fromJson(values['verb']),
          new List<PathExpr>.from(
              values['input'].map((i) => new PathExpr.fromJson(i))),
          new List<PathExpr>.from(
              values['output'].map((i) => new PathExpr.fromJson(i))),
          new List<PathExpr>.from(
              values['display'].map((i) => new PathExpr.fromJson(i))),
          new List<PathExpr>.from(
              values['compose'].map((i) => new PathExpr.fromJson(i))),
          icon: values['icon'] != null ? Uri.parse(values['icon']) : null,
          themeColor: values['themeColor'],
          schemas: schemas,
          arch: values['arch'],
          modularRevision: values['modularRevision']);
    });
  }

  String toJsonString() {
    return Timeline.timeSync("$runtimeType serialize", () => JSON.encode(this));
  }

  dynamic toJson() {
    return {
      'title': title,
      'url': url?.toString(),
      'icon': icon?.toString(),
      'themeColor': themeColor,
      'use': use,
      'verb': verb,
      'input': input,
      'output': output,
      'compose': compose,
      'display': display,
      'schemas': schemas,
      'arch': arch,
      'modularRevision': modularRevision
    };
  }

  @override
  bool operator ==(rhs) {
    return rhs is Manifest &&
        title == rhs.title &&
        url == rhs.url &&
        icon == rhs.icon &&
        themeColor == rhs.themeColor &&
        verb == rhs.verb &&
        arch == rhs.arch &&
        modularRevision == rhs.modularRevision &&
        const ListEquality().equals(input, rhs.input) &&
        const ListEquality().equals(output, rhs.output) &&
        const ListEquality().equals(compose, rhs.compose) &&
        const ListEquality().equals(display, rhs.display);
  }

  @override
  int get hashCode {
    return title.hashCode ^
        url.hashCode ^
        icon.hashCode ^
        themeColor.hashCode ^
        verb.hashCode ^
        arch.hashCode ^
        modularRevision.hashCode ^
        (input == null ? 0 : const ListEquality().hash(input)) ^
        (output == null ? 0 : const ListEquality().hash(output)) ^
        (display == null ? 0 : const ListEquality().hash(display)) ^
        (compose == null ? 0 : const ListEquality().hash(compose));
  }

  @override
  String toString() => "$runtimeType:\n"
      "title: $title\n"
      "verb: $verb\n"
      "url: $url\n"
      "input: $input\n"
      "output: $output\n"
      "display: $display\n"
      "compose: $compose\n"
      "icon: $icon\n"
      "themeColor: $themeColor\n"
      "arch: $arch\n"
      "modularRevision: $modularRevision\n"
      "src: $src\n";

  /// Used in creating dummy manifests from a recipe Step.
  static List<PathExpr> _manifestExpr(final List<PathExpr> exprs) => exprs
      .map((final PathExpr expr) => new PathExpr.single(expr.properties.last))
      .toSet()
      .toList();
}
