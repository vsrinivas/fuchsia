// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:mojo/core.dart';

import 'suggestinator_application.dart';

void main(List<String> args, Object handleToken) {
  new SuggestinatorApplication.fromHandle(new MojoHandle(handleToken));
}
