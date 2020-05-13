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
}

class Error {
  final ErrorType type;
  final String location;
  final String content;

  Error(this.type, this.location, this.content);

  Error.forProject(this.type, this.content) : location = null;

  bool get hasLocation => location != null;
}
