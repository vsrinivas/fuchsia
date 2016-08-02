// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:mojo/core.dart';

import 'handler_application.dart';

void main(List<String> args, Object handleToken) {
  MojoHandle appHandle = new MojoHandle(handleToken);
  new HandlerProviderApplication.fromHandle(appHandle);
}
