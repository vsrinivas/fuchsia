// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'module_instance.dart';

/// Runs a module implementation for a given module instance. The module runner
/// implementation obtains its inputs from its module instance and passes its
/// outputs back to the session of the module instance. It is up to the
/// implementation of the ModuleRunnerFactory (below) to decide how to actually
/// run a module. This is an abstract interface because the implementation uses
/// mojo, which we would like to keep out of the core dart library, because it
/// doesn't run in an unmodified dart vm.
abstract class ModuleRunner {
  /// Starts the module implementation. The updateOutput() method of the session
  /// of the module instance supplied will be called back with graph mutations
  /// for the outputs received from the running module implementation. The
  /// ModuleRunner uses this to send the initial state of the module's footprint
  /// graph to the module instance. The ModuleRunner is expected to construct
  /// and maintain the module's footprint graph but notify the module instance
  /// of changes only upon a call to [update()].
  void start(final ModuleInstance instance);

  /// Notifies the module runner that the module instance has changed data in
  /// its footprint (i.e., inputs or outputs). The footprint graph is obtained
  /// from the module instance and by observing the session graph and the
  /// running module implementation is notified accordingly.
  void update();

  /// Tears down the module implementation.
  void stop();
}

typedef ModuleRunner ModuleRunnerFactory();
