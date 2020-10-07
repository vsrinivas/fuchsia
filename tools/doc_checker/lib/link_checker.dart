// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';
import 'package:doc_checker/graph.dart';
import 'package:path/path.dart' as path;

import 'package:doc_checker/errors.dart';
import 'package:doc_checker/projects.dart';
import 'package:doc_checker/link_verifier.dart';

/// Information about the document containing the link to check.
class DocContext {
  String baseDir;
  String docLabel;
  Node node;
  Iterable<String> links;

  DocContext(this.baseDir, this.docLabel, this.node, this.links);
}

/// LinkChecker applies the collection of checks to a link
/// to make sure it is valid and follows the coding practices
/// for Fuchsia
class LinkChecker {
  /// Top level paths to the published doc site that any page can link to.
  /// Links to other locations have to be allowed by adding the source doc
  /// page to _filesAllowedToLinkToPublishedDocs.
  static const List<String> _publishedLinksAllowed = ['', 'reference'];

  /// Files that are allowed to link to the documentation host site.
  static const List<String> _filesAllowedToLinkToPublishedDocs = ['navbar.md'];

  /// Files that are allowed to be linked in docs. These are non-markdown files
  /// that are referenced by markdown documents.
  static const List<String> _filesAllowedUsingURI = ['OWNERS'];

  /// The fuchsia Gerrit host. Used to check if link should be http or file based.
  static const String _fuchsiaGerritHost = 'fuchsia.googlesource.com';

  /// Documentation site. Used to determine if a link should be to this host or file based.
  static const String _publishedDocsHost = 'fuchsia.dev';

  /// Different ways of pointing to the master branch of a project in a Gerrit
  /// link.
  static const List<String> _masterSynonyms = [
    'master',
    'refs/heads/master',
    'HEAD'
  ];

  final List<Error> errors = <Error>[];
  final List<Link<String>> inTreeLinks = [];
  final List<Link<String>> outOfTreeLinks = [];

  String rootDir;
  String docsDir;
  String docsProject;
  bool checkLocalLinksOnly = false;

  LinkChecker(this.rootDir, this.docsDir, this.docsProject);

  /// Checks out of tree link. Returns true if isError.
  /// Error is added to errors list.
  Future<bool> checkOutOfTreeLinks(
      Iterable<Link<String>> additionalLinks) async {
    bool foundError = false;
    // Verify http links pointing outside the tree.
    if (!checkLocalLinksOnly) {
      outOfTreeLinks.addAll(additionalLinks);
      await verifyLinks(outOfTreeLinks, (Link<String> link, bool isValid) {
        if (!isValid) {
          errors.add(
              Error(ErrorType.brokenLink, link.payload, link.uri.toString()));
          foundError = true;
        }
      });
    }
    return foundError;
  }

  Future<bool> checkInTreeLinks() async {
    bool foundError = false;
    // Verify http links pointing inside the tree just by checking to see if the
    // path exists, as HTTP calls would be unnecessarily expensive here.
    for (Link<String> link in inTreeLinks) {
      final File possibleFile = File.fromUri(link.uri);
      final Directory possibleDir = Directory.fromUri(link.uri);
      /*
      * Check that the link is one of:
          a file that exists
          a directory that exists outside the /docs/ directory, such as a source directory.foundError
          a directory within the docs directory and it has a README.md file in that directory.
      */
      if (possibleFile.existsSync()) {
        continue;
      } else if (possibleDir.existsSync()) {
        // Check for README.md or being outside the /docs/ directory.
        if (possibleDir.path.contains('/docs/') &&
            !File('${possibleDir.path}/README.md').existsSync()) {
          errors.add(Error(
              ErrorType.invalidLinkToDirectory, link.payload, link.toString()));
          foundError = true;
        }
      } else {
        // Neither the file nor the directory exist, record and error.
        errors.add(Error(ErrorType.brokenLink, link.payload, link.toString()));
        foundError = true;
      }
    }
    return foundError;
  }

  /// Checks whether the URI points to the master branch of a Gerrit (i.e.,
  /// googlesource.com) project.
  bool onGerritMaster(Uri uri) {
    final int index = uri.pathSegments.indexOf('+');
    if (index == -1 || index == uri.pathSegments.length - 1) {
      return false;
    }
    final String subPath = uri.pathSegments.sublist(index + 1).join('/');
    for (String branch in _masterSynonyms) {
      if (subPath.startsWith(branch)) {
        return true;
      }
    }
    return false;
  }

  /// Checks the given link, returning true if there is an error.
  /// The error is added to the errors field.
  bool checkLink(DocContext doc, String link,
      Function(String docPath, DocContext doc, String linkLabel) onNewEdge) {
    // Parse link to URI
    Uri uri;
    try {
      uri = Uri.parse(link);
    } on FormatException {
      errors.add(Error(ErrorType.invalidUri, doc.docLabel, link));
      return true;
    }

    // Check URI that have a scheme. Files are handled with the scheme-less.
    if (uri.hasScheme && uri.scheme != 'file') {
      // Ignore non http schemes.
      if (uri.scheme != 'http' && uri.scheme != 'https') {
        return false;
      }
      final bool linkToFuchsiaGerritHost = uri.authority == _fuchsiaGerritHost;
      final bool linkToPublishedDocsHost = uri.authority == _publishedDocsHost;
      final String project =
          uri.pathSegments.isEmpty ? '' : uri.pathSegments[0];

      // Check links back to the gerrit host server.
      if (linkToFuchsiaGerritHost) {
        if (onGerritMaster(uri) && project == docsProject) {
          // Check for doc exception Files
          final int index = uri.pathSegments.indexOf('docs');
          String subPath = uri.path;
          if (index >= 0) {
            subPath = uri.pathSegments[uri.pathSegments.length - 1];
          }
          if (!_filesAllowedUsingURI.contains(subPath)) {
            errors.add(Error(
                ErrorType.convertHttpToPath, doc.docLabel, uri.toString()));
            return true;
          }
        } else if (!validProjects.contains(project)) {
          errors.add(
              Error(ErrorType.obsoleteProject, doc.docLabel, uri.toString()));
          return true;
        }
        return false;
      }

      // Check links to the published docs server.
      if (linkToPublishedDocsHost &&
          !_publishedLinksAllowed.contains(project)) {
        if (!_filesAllowedToLinkToPublishedDocs
            .contains(path.basename(doc.docLabel))) {
          errors.add(
              Error(ErrorType.convertHttpToPath, doc.docLabel, uri.toString()));
          return true;
        }
      } else {
        outOfTreeLinks.add(Link(uri, doc.docLabel));
        return false;
      }
    } else {
      // Handle non-schemed URI.
      final List<String> parts = uri.path.split('#');
      final String location = parts[0];
      // TODO(wilkinsonclay): Add anchor name checks.
      if (location.isEmpty) {
        return false;
      }

      final String rootRelPath = location.startsWith('/')
          ? location.substring(1)
          : path.relative(path.join(doc.baseDir, location), from: rootDir);
      final String absPath = path.join(rootDir, rootRelPath);
      final String linkLabel = '//$rootRelPath';
      final Uri localUri = Uri.parse('file://$absPath');

      // Callback for the graph building.
      if (onNewEdge != null) {
        onNewEdge(absPath, doc, linkLabel);
      }

      // Links that reference a parent dir past root dir have a path of / when parsed to URIs.
      // When this happens, the rootRelPath is empty, so flag this as an invalid path.
      if (rootRelPath.isEmpty) {
        errors.add(Error(ErrorType.invalidRelativePath, doc.docLabel, link));
        return true;
      }

      if (location.contains('../') &&
          !localUri.toString().startsWith('file://$docsDir')) {
        errors
            .add(Error(ErrorType.invalidRelativePath, doc.docLabel, location));
        return true;
      }

      inTreeLinks.add(Link(localUri, doc.docLabel));
      return false;
    }
    return false;
  }

  Future<bool> check(
      Iterable<DocContext> docList,
      Iterable<Link<String>> additionalOutOfTreeLinks,
      Function(String docPath, DocContext doc, String linkLabel)
          onNewEdge) async {
    bool foundError = false;

    for (DocContext doc in docList) {
      for (String link in doc.links) {
        foundError |= checkLink(doc, link, onNewEdge);
      }
    }

    foundError |= await checkInTreeLinks();
    foundError |= await checkOutOfTreeLinks(additionalOutOfTreeLinks);

    return foundError;
  }
}
