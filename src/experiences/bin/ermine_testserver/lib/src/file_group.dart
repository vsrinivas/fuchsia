// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/68320): Add more test cases.
enum TestCase {
  navigation,
}

/// Defines group of web files in the test case.
class FileGroup {
  final String groupName;
  final List<String> fileList;

  FileGroup(this.groupName, this.fileList);

  String getPath(int index) => '/pkg/data/$groupName/${fileList[index]}';
}

final fileGroups = <TestCase, FileGroup>{
  TestCase.navigation: FileGroup(
    'navigation',
    <String>[
      'index.html',
      'next.html',
      'popup.html',
      'style.css',
    ],
  )
};
