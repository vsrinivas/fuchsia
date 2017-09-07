// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

library bindings;

import 'dart:async';
import 'dart:collection';
import 'dart:convert';
import 'dart:typed_data';
import 'dart:zircon';

import 'package:lib.fidl.dart/core.dart' as core;
import 'package:meta/meta.dart';

part 'src/bindings/codec.dart';
part 'src/bindings/enum.dart';
part 'src/bindings/interface.dart';
part 'src/bindings/message.dart';
part 'src/bindings/struct.dart';
part 'src/bindings/union.dart';
