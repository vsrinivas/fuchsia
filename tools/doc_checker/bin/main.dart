// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:args/args.dart';
import 'package:path/path.dart' as path;

import 'package:doc_checker/errors.dart';
import 'package:doc_checker/graph.dart';
import 'package:doc_checker/link_checker.dart';
import 'package:doc_checker/link_scraper.dart';
import 'package:doc_checker/image_scraper.dart';
import 'package:doc_checker/yaml_checker.dart';

const String _optionHelp = 'help';
const String _optionRootDir = 'root';
const String _optionProject = 'project';
const String _optionDotFile = 'dot-file';
const String _optionLocalLinksOnly = 'local-links-only';

// Documentation subdirectory to inspect.
const String _docsDir = 'docs';

void reportError(Error error) {
  print(error);
}

Future<Null> main(List<String> args) async {
  final ArgParser parser = ArgParser()
    ..addFlag(
      _optionHelp,
      help: 'Displays this help message.',
      negatable: false,
    )
    ..addOption(
      _optionRootDir,
      help: 'Path to the root of the checkout',
      defaultsTo: '.',
    )
    ..addOption(
      _optionProject,
      help: 'Name of the project being inspected',
      defaultsTo: 'fuchsia',
    )
    ..addOption(
      _optionDotFile,
      help: 'Path to the dotfile to generate',
      defaultsTo: '',
    )
    ..addFlag(
      _optionLocalLinksOnly,
      help: 'Don\'t attempt to resolve http(s) links',
      negatable: false,
    );
  final ArgResults options = parser.parse(args);

  if (options[_optionHelp]) {
    print(parser.usage);
    return;
  }

  final String rootDir = path.canonicalize(options[_optionRootDir]);
  final String docsProject = options[_optionProject];
  final String docsDir = path.canonicalize(path.join(rootDir, _docsDir));

  final List<String> docs = Directory(docsDir)
      .listSync(recursive: true)
      .where((FileSystemEntity entity) =>
          path.extension(entity.path) == '.md' &&
          // Skip these files created by macOS since they're not real Markdown:
          // https://apple.stackexchange.com/q/14980
          !path.basename(entity.path).startsWith('._'))
      .map((FileSystemEntity entity) => entity.path)
      .toList();

  final String readme = path.join(docsDir, 'README.md');
  final Graph graph = Graph();
  final List<Error> errors = <Error>[];

  LinkChecker linkChecker = LinkChecker(rootDir, docsDir, docsProject)
    ..checkLocalLinksOnly = options[_optionLocalLinksOnly];

  final List<DocContext> docContextList = [];
  for (String doc in docs) {
    final String docLabel = '//${path.relative(doc, from: rootDir)}';
    final String baseDir = path.dirname(doc);
    final Node node = graph.getNode(docLabel);
    if (doc == readme) {
      graph.root = node;
    }
    // Check alt text for images.
    for (ImageData img in ImageScraper().scrape(doc)) {
      if (img.alt.isEmpty) {
        errors.add(Error(ErrorType.missingAltText, docLabel, img.src));
      }
    }

    docContextList
        .add(DocContext(baseDir, docLabel, node, LinkScraper().scrape(doc)));
  }

  // Check yaml files
  final List<String> yamls = Directory(docsDir)
      .listSync(recursive: true)
      .where((FileSystemEntity entity) =>
          path.extension(entity.path) == '.yaml' &&
          path.basename(entity.path).startsWith('_'))
      .map((FileSystemEntity entity) => entity.path)
      .toList();

  // Start with the /docs/_toc.yaml as the root file.
  final String rootYaml = path.canonicalize(path.join(docsDir, '_toc.yaml'));

  YamlChecker checker = YamlChecker(rootDir, rootYaml, yamls, docs);
  await checker.check();
  List<Error> yamlErrors = checker.errors;

  if (yamlErrors.isNotEmpty) {
    errors.addAll(yamlErrors);
  }

  // Check links
  await linkChecker.check(docContextList, checker.outOfTreeLinks,
      (String docPath, DocContext doc, String linkLabel) {
    if (docs.contains(docPath)) {
      graph.addEdge(from: doc.node, to: graph.getNode(linkLabel));
    }
  });

  List<Error> linkErrors = linkChecker.errors;
  if (linkErrors.isNotEmpty) {
    errors.addAll(linkErrors);
  }

  errors
    ..sort((Error a, Error b) => a.type.index - b.type.index)
    ..forEach(print);

  if (options[_optionDotFile].isNotEmpty) {
    graph.export('fuchsia_docs', File(options[_optionDotFile]).openWrite());
  }

  if (errors.isNotEmpty) {
    print('Found ${errors.length} error(s).');
    exitCode = 1;
  }
}
