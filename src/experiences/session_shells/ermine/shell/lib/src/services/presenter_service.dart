// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:ui';

// ignore: directives_ordering
import 'package:ermine/src/states/view_state.dart';
import 'package:ermine/src/states/view_state_impl.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_session/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:fuchsia_services/services.dart';
import 'package:zircon/zircon.dart';

typedef ViewPresentedCallback = bool Function(ViewState viewState);
typedef ViewDismissedCallback = void Function(ViewState viewState);
typedef ErrorCallback = void Function(String url, String error);

/// Defines a [GraphicalPresenter] to present and dismiss application views.
class PresenterService extends GraphicalPresenter {
  late ViewPresentedCallback onViewPresented;
  late ViewDismissedCallback onViewDismissed;
  late VoidCallback onPresenterDisposed;
  late ErrorCallback onError;

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

    // Build title from one of: name, url, id or ''.
    final title = name ?? (url?.startsWith('http') == true ? url : id ?? '');

    final viewHolderToken = viewSpec.viewHolderToken;
    final viewRef = viewSpec.viewRef;
    if (viewHolderToken != null && viewRef != null) {
      final viewConnection = FuchsiaViewConnection(
        viewHolderToken,
        viewRef: viewRef,
        onViewStateChanged: (_, state) {
          viewState.viewStateChanged(state: state ?? false);
        },
      );

      final viewRefDup =
          ViewRef(reference: viewRef.reference.duplicate(ZX.RIGHT_SAME_RIGHTS));

      viewState = ViewStateImpl(
        viewConnection: viewConnection,
        view: ViewHandle(viewRefDup),
        id: id,
        title: title!,
        url: url,
        onClose: viewController.close,
      );
      onViewPresented(viewState);
    } else {
      final error = ViewControllerEpitaph.invalidViewSpec;
      if (url != null) {
        onError(url, error.toString());
      }
      viewController.close(error);
    }
  }

  // Holds the fidl binding to this implementation of [GraphicalPresenter].
  final _binding = GraphicalPresenterBinding();
  bool _disposed = false;

  // Binds the request to this service.
  void bind(InterfaceRequest<GraphicalPresenter> request) {
    _binding
      ..bind(this, request)
      ..whenClosed.then((_) {
        _disposed = true;
        onPresenterDisposed();
      });
  }

  void dispose() {
    if (!_disposed && !_binding.isClosed) {
      _binding.close(0);
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
    // ignore: unawaited_futures
    onDismiss.call();
    close();
  }

  @override
  Stream<void> get onPresented => _onPresentedStreamController.stream;

  void didPresent() {
    _onPresentedStreamController.add(null);
  }
}
