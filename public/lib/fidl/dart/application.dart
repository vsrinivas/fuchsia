// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

library application;

import 'dart:async';
import 'dart:collection';

import 'package:lib.fidl.dart/bindings.dart' as bindings;
import 'package:lib.fidl.dart/core.dart' as core;
import 'package:mojo.public.interfaces.application/application.mojom.dart' as application_mojom;
import 'package:mojo.public.interfaces.application/service_provider.mojom.dart';
import 'package:mojo.public.interfaces.application/shell.mojom.dart' as shell_mojom;
import 'package:mojo.public.interfaces.bindings/service_describer.mojom.dart'
    as service_describer;

part 'src/application.dart';
part 'src/application_connection.dart';
part 'src/service_describer.dart';
