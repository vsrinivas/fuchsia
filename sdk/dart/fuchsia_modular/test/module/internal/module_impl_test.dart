// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.9

import 'package:fidl_fuchsia_modular/fidl_async.dart' as fidl;
import 'package:fuchsia_modular/lifecycle.dart';
import 'package:fuchsia_modular/src/module/internal/_module_impl.dart'; // ignore: implementation_imports
import 'package:fuchsia_services/services.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

// Mock classes
class MockLifecycle extends Mock implements Lifecycle {}

class MockModuleContext extends Mock implements fidl.ModuleContext {}

void main() {
  setUpAll(() {
    final context = ComponentContext.createAndServe();
    Lifecycle.enableLifecycleEvents(context.outgoing);
  });

  test('verify Lifecycle init during the construction of ModuleImpl', () {
    final mockLifecycle = MockLifecycle();
    ModuleImpl(lifecycle: mockLifecycle);
    verify(mockLifecycle.addTerminateListener(any));
  });

  test('verify removeSelfFromStory should call context.removeSelfFromStory',
      () {
    final mockContext = MockModuleContext();
    ModuleImpl(moduleContext: mockContext).removeSelfFromStory();
    verify(mockContext.removeSelfFromStory());
  });
}
