// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:ui';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_session/fidl_async.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:fuchsia_services/services.dart';

import 'package:next/src/states/view_state.dart';
import 'package:next/src/states/view_state_impl.dart';
import 'package:next/src/utils/view_handle.dart';

typedef ViewPresentedCallback = bool Function(ViewState viewState);
typedef ViewDismissedCallback = void Function(ViewState viewState);

/// Defines a [GraphicalPresenter] to present and dismiss application views.
class PresenterService extends GraphicalPresenter {
  late ViewPresentedCallback onViewPresented;
  late ViewDismissedCallback onViewDismissed;

  PresenterService();

  void advertise(Outgoing outgoing) {
    outgoing.addPublicService(bind, GraphicalPresenter.$serviceName);
  }

  @override
  Future<void> presentView(ViewSpec viewSpec,
      InterfaceRequest<ViewController>? viewControllerRequest) async {
    late ViewStateImpl viewState;
    final viewController = _ViewControllerImpl(() => onViewDismissed(viewState))
      ..bind(viewControllerRequest);

    const nullAnnotation = Annotation(key: '__null__');
    // Check to see if we have an id that we included in the annotation.
    final id = viewSpec.annotations?.customAnnotations
        ?.firstWhere((a) => a.key == 'id', orElse: () => nullAnnotation)
        .value
        ?.text;
    final url = viewSpec.annotations?.customAnnotations
        ?.firstWhere((a) => a.key == 'url', orElse: () => nullAnnotation)
        .value
        ?.text;
    final name = viewSpec.annotations?.customAnnotations
        ?.firstWhere((a) => a.key == 'name', orElse: () => nullAnnotation)
        .value
        ?.text;

    final viewHolderToken = viewSpec.viewHolderToken;
    if (viewHolderToken != null) {
      final viewConnection = FuchsiaViewConnection(
        viewHolderToken,
        viewRef: viewSpec.viewRef,
        onViewStateChanged: (_, state) {
          viewState.viewStateChanged(state: state ?? false);
        },
      );

      viewState = ViewStateImpl(
        viewConnection: viewConnection,
        view: ViewHandle(viewSpec.viewRef!),
        id: id,
        title: name ?? id ?? url ?? '',
        url: url,
        onClose: viewController.close,
      );
      onViewPresented(viewState);
    } else {
      viewController.close(ViewControllerEpitaph.invalidViewSpec);
    }
  }

  // Holds the fidl bindings to this implementation of [GraphicalPresenter].
  final List<GraphicalPresenterBinding> _bindings = [];

  // Binds the request to this service.
  void bind(InterfaceRequest<GraphicalPresenter> request) {
    final binding = GraphicalPresenterBinding();
    binding
      ..bind(this, request)
      ..whenClosed.then((_) {
        _bindings.remove(binding);
      });
    _bindings.add(binding);
  }

  void dispose() {
    for (final binding in _bindings) {
      binding.close(0);
    }
  }
}

class _ViewControllerImpl extends ViewController {
  final _binding = ViewControllerBinding();
  final StreamController<void> _onPresentedStreamController =
      StreamController.broadcast();
  VoidCallback onDismiss;

  _ViewControllerImpl(this.onDismiss);

  void bind(InterfaceRequest<ViewController>? interfaceRequest) {
    _binding.bind(this, interfaceRequest!);
  }

  void close([ViewControllerEpitaph? e]) {
    if (_binding.isBound) {
      _binding.close(e?.$value ?? 0);
    }
    _onPresentedStreamController.close();
  }

  @override
  Future<void> annotate(Annotations annotations) async {
    //TODO(47791) need to implement this method
  }

  @override
  Future<void> dismiss() async {
    onDismiss.call();
    close();
  }

  @override
  Stream<void> get onPresented => _onPresentedStreamController.stream;

  void didPresent() {
    _onPresentedStreamController.add(null);
  }
}
