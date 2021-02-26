// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports
import 'package:fuchsia_modular/src/module/module_state_exception.dart';
import 'package:test/test.dart';

/// A matcher which matches on the ModuleStateException type.
const Matcher isModuleStateException = TypeMatcher<ModuleStateException>();

/// A matcher which matches on the module ModuleStateException type.
Matcher throwsModuleStateException = throwsA(isModuleStateException);
