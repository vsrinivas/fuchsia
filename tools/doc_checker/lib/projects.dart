// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

/// List of active repos under fuchsia.googlesource.com which can be linked to.
const List<String> validProjects = <String>[
  '', // root page of all projects
  'cobalt',
  'drivers', // This is a family of projects.
  'experiences',
  'fargo',
  'fidl-misc',
  'fidlbolt',
  'fontdata',
  'fuchsia',
  'infra', // This is a family of projects.
  'integration',
  'intellij-language-fidl',
  'jiri',
  'manifest',
  'third_party', // This is a family of projects.
  'topaz',
  'vscode-language-fidl',
  'workstation',
  'samples',
  'sdk-samples', // This is a family of projects.
];
