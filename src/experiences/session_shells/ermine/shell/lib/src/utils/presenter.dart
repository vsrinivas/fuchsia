// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_session/fidl_async.dart' as fidl;
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/foundation.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:internationalization/strings.dart';

import 'suggestion.dart';

/// A callback which is invoked when the presenter is asked to present a view.
///
/// The ViewControllerImpl will not be null but it may not be bound if the
/// requesting source did not provide a channel. The methods will still be safe
/// to call even if it is not bound.
typedef PresentViewCallback = void Function(
    FuchsiaViewConnection, ViewRef, ViewControllerImpl, String, String, String);

/// A callback which is invoked when the element is dismissed by the session.
///
/// This callback is the result of the session getting notified of a component
/// exiting by itself or a crash. It is NOT due to the user closing it from
/// the shell.
typedef DismissViewCallback = void Function(ViewControllerImpl);

typedef AlertCallback = void Function(String, [String, String]);

/// A service which implements the fuchsia.session.GraphicalPresenter protocol.
class PresenterService extends fidl.GraphicalPresenter {
  static const String serviceName = fidl.GraphicalPresenter.$serviceName;

  final List<fidl.GraphicalPresenterBinding> _bindings = [];
  final PresentViewCallback onPresent;
  final DismissViewCallback onDismiss;
  final AlertCallback onError;

  PresenterService({this.onPresent, this.onDismiss, this.onError});

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
    final viewController = ViewControllerImpl(onDismiss)
      ..bind(viewControllerRequest);

    // Check to see if we have an id that we included in the annotation.
    final idAnnotation = viewSpec.annotations?.customAnnotations
        ?.firstWhere((a) => a.key == ermineSuggestionIdKey, orElse: () => null);
    final urlAnnotation = viewSpec.annotations?.customAnnotations
        ?.firstWhere((a) => a.key == 'url', orElse: () => null);
    final nameAnnotation = viewSpec.annotations?.customAnnotations
        ?.firstWhere((a) => a.key == 'name', orElse: () => null);

    final viewHolderToken = viewSpec.viewHolderToken;
    if (viewHolderToken != null) {
      final connection = FuchsiaViewConnection(
        viewHolderToken,
        viewRef: viewSpec.viewRef,
        onViewStateChanged: (_, state) {
          viewController.stateChanged.value = state;
          if (state == true) {
            viewController.viewRendered.value = true;
          }
        },
      );
      onPresent(
        connection,
        viewSpec.viewRef,
        viewController,
        idAnnotation?.value?.text,
        urlAnnotation?.value?.text,
        nameAnnotation?.value?.text,
      );
    } else {
      final name = nameAnnotation?.value?.text;
      final url = nameAnnotation?.value?.text;
      final title = Strings.presentViewErrorTitle;
      final header = name ?? url ?? 'Unknown Element';
      final description = 'ViewControllerEpitaph.INVALID_VIEW_SPEC:\n'
          '${Strings.presentViewErrorDesc}';
      onError?.call(title, header, description);
      viewController.close(fidl.ViewControllerEpitaph.invalidViewSpec);
    }
  }
}

class ViewControllerImpl extends fidl.ViewController {
  /// Notifier for view state change callback.
  ValueNotifier stateChanged = ValueNotifier(null);
  ValueNotifier viewRendered = ValueNotifier(false);

  final _binding = fidl.ViewControllerBinding();
  final StreamController<void> _onPresentedStreamController =
      StreamController.broadcast();
  DismissViewCallback onDismiss;

  ViewControllerImpl(this.onDismiss);

  void bind(InterfaceRequest<fidl.ViewController> interfaceRequest) {
    if (interfaceRequest != null && interfaceRequest.channel != null) {
      _binding.bind(this, interfaceRequest);
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
    onDismiss?.call(this);
    close();
  }

  @override
  Stream<void> get onPresented => _onPresentedStreamController.stream;

  void didPresent() {
    _onPresentedStreamController.add(null);
  }
}
