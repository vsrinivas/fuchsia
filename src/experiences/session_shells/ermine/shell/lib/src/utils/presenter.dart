// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fuchsia_session/fidl_async.dart' as fidl;
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:fidl/fidl.dart';
import 'package:fuchsia_logger/logger.dart';

import 'suggestion.dart';

/// A callback which is invoked when the presenter is asked to present a view.
///
/// The ViewControllerImpl will not be null but it may not be bound if the
/// requesting source did not provide a channel. The methods will still be safe
/// to call even if it is not bound.
typedef PresentViewCallback = void Function(
    ViewHolderToken, ViewControllerImpl, String);

/// A service which implements the fuchsia.session.GraphicalPresenter protocol.
class PresenterService extends fidl.GraphicalPresenter {
  static const String serviceName = fidl.GraphicalPresenter.$serviceName;

  final List<fidl.GraphicalPresenterBinding> _bindings = [];
  final PresentViewCallback _onPresent;

  PresenterService(this._onPresent);

  /// Binds the request to this model.
  void bind(InterfaceRequest<fidl.GraphicalPresenter> request) {
    final binding = fidl.GraphicalPresenterBinding();
    binding
      ..bind(this, request)
      ..whenClosed.then((_) {
        _bindings.remove(binding);
      });
    _bindings.add(binding);
  }

  @override
  Future<void> presentView(fidl.ViewSpec viewSpec,
      InterfaceRequest<fidl.ViewController> viewControllerRequest) async {
    final viewController = ViewControllerImpl()..bind(viewControllerRequest);

    // Check to see if we have an id that we included in the annotation.
    final idAnnotation = viewSpec.annotations?.customAnnotations
        ?.firstWhere((a) => a.key == ermineSuggestionIdKey, orElse: () => null);

    final viewHolderToken = viewSpec.viewHolderToken;
    if (viewHolderToken != null) {
      _onPresent(
          viewHolderToken, viewController, idAnnotation?.value?.text ?? '');
    } else {
      viewController.close(fidl.ViewControllerEpitaph.invalidViewSpec);
    }
  }
}

class ViewControllerImpl extends fidl.ViewController {
  final _binding = fidl.ViewControllerBinding();
  final StreamController<void> _onPresentedStreamController =
      StreamController.broadcast();

  void bind(InterfaceRequest<fidl.ViewController> interfaceRequest) {
    if (interfaceRequest != null && interfaceRequest.channel != null) {
      _binding.bind(this, interfaceRequest);
      _binding.whenClosed.then((_) {
        //TODO(47793) Need to watch our binding for unexpected closures and notify the user
        log.info('binding closed unexpectedly');
      });
    }
  }

  void close([fidl.ViewControllerEpitaph e]) {
    if (_binding.isBound) {
      _binding.close(e?.$value);
    }
    _onPresentedStreamController.close();
  }

  @override
  Future<void> annotate(fidl.Annotations annotations) async {
    //TODO(47791) need to implement this method
  }

  @override
  Future<void> dismiss() async {
    // TODO(47792): A call to dismiss indicates that the view should go away we need to
    // allow the user of this class to asynchronously dismiss the view before closing
    // the channel.
    close();
  }

  @override
  Stream<void> get onPresented => _onPresentedStreamController.stream;

  void didPresent() {
    _onPresentedStreamController.add(null);
  }
}
