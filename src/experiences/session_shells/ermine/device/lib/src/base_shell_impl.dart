// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_auth/fidl_async.dart';
import 'package:fidl_fuchsia_modular/fidl_async.dart';
import 'package:fidl_fuchsia_ui_policy/fidl_async.dart';

/// Called when [BaseShell.initialize] occurs.
typedef OnBaseShellReady = void Function(
  UserProvider userProvider,
  BaseShellContext baseShellContext,
  Presentation presentation,
);

/// Called when [Lifecycle.terminate] occurs.
typedef OnBaseShellStop = void Function();

/// Implements a BaseShell for receiving the services a [BaseShell] needs to
/// operate.
class BaseShellImpl extends BaseShell implements Lifecycle {
  final BaseShellContextProxy _baseShellContextProxy =
      BaseShellContextProxy();
  final UserProviderProxy _userProviderProxy = UserProviderProxy();
  final PresentationProxy _presentationProxy = PresentationProxy();
  final Set<AuthenticationUiContextBinding> _authUiContextBindingSet =
      <AuthenticationUiContextBinding>{};

  /// Called when [initialize] occurs.
  final OnBaseShellReady onReady;

  /// Called when the [BaseShell] terminates.
  final OnBaseShellStop onStop;

  /// The [AuthenticationUiContext] is a new interface from
  /// |fuchsia::auth::TokenManager| service that provides a new authentication
  /// UI context to display signin and permission screens when requested.
  final AuthenticationUiContext authenticationUiContext;

  /// Constructor.
  BaseShellImpl({
    this.authenticationUiContext,
    this.onReady,
    this.onStop,
  });
  @override
  Future<void> initialize(
    InterfaceHandle<BaseShellContext> baseShellContextHandle,
    BaseShellParams baseShellParams,
  ) async {
    if (onReady != null) {
      _baseShellContextProxy.ctrl.bind(baseShellContextHandle);
      await _baseShellContextProxy
          .getUserProvider(_userProviderProxy.ctrl.request());
      await _baseShellContextProxy
          .getPresentation(_presentationProxy.ctrl.request());
      onReady(_userProviderProxy, _baseShellContextProxy, _presentationProxy);
    }
  }

  @override
  Future<void> terminate() async {
    onStop?.call();
    _userProviderProxy.ctrl.close();
    _baseShellContextProxy.ctrl.close();
    for (AuthenticationUiContextBinding binding in _authUiContextBindingSet) {
      binding.close();
    }
  }

  @override
  Future<void> getAuthenticationUiContext(
    InterfaceRequest<AuthenticationUiContext> request,
  ) async {
    AuthenticationUiContextBinding binding =
        AuthenticationUiContextBinding()
          ..bind(authenticationUiContext, request);
    _authUiContextBindingSet.add(binding);
  }

  /// Closes all bindings to authentication contexts, effectively cancelling any ongoing
  /// authorization flows.
  void closeAuthenticationUiContextBindings() {
    for (AuthenticationUiContextBinding binding in _authUiContextBindingSet) {
      binding.close();
    }
    _authUiContextBindingSet.clear();
  }
}
