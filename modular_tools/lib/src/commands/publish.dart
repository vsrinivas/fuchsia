// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:args/command_runner.dart';

class PublishCommand extends Command {
  final String name = 'publish';
  final String description =
      'Publish this module to the CDN and update the index.';

  @override
  Future<int> run() async {
    print('Command unimplemented.');
    return 2;
  }
}
