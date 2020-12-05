// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

enum ErrorType {
  unknownLocalFile,
  convertHttpToPath,
  brokenLink,
  unreachablePage,
  obsoleteProject,
  invalidMenu,
  invalidUri,
  missingAltText,
  invalidRelativePath,
  invalidLinkToDirectory,
  unparseableYaml,
}

class Error {
  final ErrorType type;
  final String location;
  final String content;

  Error(this.type, this.location, this.content);

  Error.forProject(this.type, this.content) : location = null;

  bool get hasLocation => location != null;

  @override
  String toString() {
    String str;
    switch (type) {
      case ErrorType.unknownLocalFile:
        str = 'Linking to unknown file';
        break;
      case ErrorType.convertHttpToPath:
        str = 'Convert http to path';
        break;
      case ErrorType.brokenLink:
        str = 'Http link is broken';
        break;
      case ErrorType.unreachablePage:
        str = 'Page should be reachable';
        break;
      case ErrorType.obsoleteProject:
        str =
            'Project or repo that this URL refers to is obsolete (not in validProjects list)';
        break;
      case ErrorType.invalidMenu:
        str = 'Invalid Menu Entry';
        break;
      case ErrorType.invalidUri:
        str = 'Invalid URI';
        break;
      case ErrorType.missingAltText:
        str = 'Missing Alt text on image';
        break;
      case ErrorType.invalidRelativePath:
        str =
            'Relative paths cannot go past //docs. Use a path starting with /';
        break;
      case ErrorType.invalidLinkToDirectory:
        str = 'Invalid link to directory. Directory must have a README.md file';
        break;
      case ErrorType.unparseableYaml:
        str = 'Cannot parse YAML file';
        break;
      default:
        str = 'Unknown error type $type';
        break;
    }
    final String locStr = hasLocation ? ' ($location)' : '';
    return '${str.padRight(25)}: $content$locStr';
  }
}
