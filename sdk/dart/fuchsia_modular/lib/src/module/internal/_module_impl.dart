// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_modular/fidl_async.dart' as modular;
import 'package:fuchsia_modular/lifecycle.dart';

import '../module.dart';
import '_module_context.dart';

/// A concrete implementation of the [Module] interface. This class
/// is not intended to be used directly by authors but instead should
/// be used by the [Module] factory constructor.
class ModuleImpl implements Module {
  /// Returns the [fidl.ModuleContext] for the running module.
  /// This variable should not be used directly. Use the
  /// [getContext()] method instead
  modular.ModuleContext? _moduleContext;

  /// The default constructor for this instance.
  ///
  /// the [moduleContext] is an optional parameter that
  /// can be supplied to override the default module context.
  /// This is mainly useful in testing scenarios.
  ModuleImpl({
    Lifecycle? lifecycle,
    modular.ModuleContext? moduleContext,
  }) : _moduleContext = moduleContext {
    (lifecycle ??= Lifecycle()).addTerminateListener(_terminate);
  }

  @override
  void removeSelfFromStory() {
    _getContext().removeSelfFromStory();
  }

  modular.ModuleContext _getContext() => _moduleContext ??= getModuleContext();

  // any necessary cleanup should be done in this method.
  Future<void> _terminate() async {}
}

/// When Module resolution fails.
class ModuleResolutionException implements Exception {
  /// Information about the failure.
  final String message;

  /// Create a new [ModuleResolutionException].
  ModuleResolutionException(this.message);
}
