// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:ui';

// ignore: directives_ordering
import 'package:ermine/src/services/launch_service.dart';
import 'package:ermine/src/states/view_state.dart';
import 'package:ermine/src/states/view_state_impl.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_element/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:fuchsia_services/services.dart';
import 'package:zircon/zircon.dart';

typedef ViewPresentedCallback = bool Function(ViewState viewState);
typedef ViewDismissedCallback = void Function(ViewState viewState);
typedef ErrorCallback = void Function(String url, String error);

class PresenterService {
  late ViewPresentedCallback onViewPresented;
  late ViewDismissedCallback onViewDismissed;
  late VoidCallback onPresenterDisposed;
  late ErrorCallback onError;

  PresenterService();

  final _bindings = <_GraphicalPresenterImpl>{};

  void advertise(Outgoing outgoing) {
    outgoing.addPublicService(bind, GraphicalPresenter.$serviceName);
  }

  void bind(InterfaceRequest<GraphicalPresenter> request) {
    final graphicalPresenter = _GraphicalPresenterImpl()
      ..onPresenterDisposed = onPresenterDisposed
      ..onViewPresented = onViewPresented
      ..onViewDismissed = onViewDismissed
      ..onError = onError
      ..bind(request);
    _bindings.add(graphicalPresenter);
  }

  void dispose() {
    for (var binding in _bindings) {
      binding.dispose();
    }
  }
}

/// Defines a [GraphicalPresenter] to present and dismiss application views.
class _GraphicalPresenterImpl extends GraphicalPresenter {
  late ViewPresentedCallback onViewPresented;
  late ViewDismissedCallback onViewDismissed;
  late VoidCallback onPresenterDisposed;
  late ErrorCallback onError;

  _GraphicalPresenterImpl();

  @override
  Future<void> presentView(
      ViewSpec viewSpec,
      InterfaceHandle<AnnotationController>? annotationController,
      InterfaceRequest<ViewController>? viewControllerRequest) async {
    late ViewStateImpl viewState;

    _ViewControllerImpl? viewController;
    if (viewControllerRequest != null) {
      viewController = _ViewControllerImpl(() => onViewDismissed(viewState))
        ..bind(viewControllerRequest);
    }

    // Check to see if we have an id that we included in the annotation.
    final id = _getAnnotation(viewSpec.annotations, 'id') ??
        '${DateTime.now().millisecondsSinceEpoch}';
    final url = _getAnnotation(viewSpec.annotations, 'url',
        namespace: elementManagerNamespace);
    final name = _getAnnotation(viewSpec.annotations, 'name');

    // Build title from one of: name, url or id.
    final title = name ??
        (url?.startsWith('http') == true
            ? url // Use the http url as title.
            : url?.contains('#meta/') == true // Fuchsia package url.
                // Extract title after '#meta/' and before '.cmx'.
                ? url?.split('#meta/')[1].split('.cm')[0] ?? id
                : id);

    final viewRef = viewSpec.viewRef;
    if (!_validateViewSpec(viewSpec, url)) {
      viewController?.close();
      throw MethodException(PresentViewError.invalidArgs);
    }

    final useFlatland = viewSpec.viewportCreationToken != null;
    // TODO(fxbug.dev/86649): Instead of passing |viewRef| we should let the
    // child send us the one they minted for Flatland.
    final viewConnection = useFlatland
        ? FuchsiaViewConnection.flatland(
            viewSpec.viewportCreationToken,
            viewRef: viewRef,
            onViewConnected: (_) => viewState.viewConnected(),
            onViewStateChanged: (_, state) {
              viewState.viewStateChanged(state: state ?? false);
            },
          )
        : FuchsiaViewConnection(
            viewSpec.viewHolderToken,
            viewRef: viewRef,
            onViewConnected: (_) => viewState.viewConnected(),
            onViewStateChanged: (_, state) {
              viewState.viewStateChanged(state: state ?? false);
            },
          );

    final viewRefDup =
        ViewRef(reference: viewRef!.reference.duplicate(ZX.RIGHT_SAME_RIGHTS));

    viewState = ViewStateImpl(
      viewConnection: viewConnection,
      view: ViewHandle(viewRefDup),
      id: id,
      title: title!,
      url: url,
      onClose: viewController?.close ?? () {},
    );
    onViewPresented(viewState);
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
      });
  }

  void dispose() {
    if (!_disposed && !_binding.isClosed) {
      _binding.close(0);
    }
  }

  String? _getAnnotation(List<Annotation>? annotations, String name,
      {String namespace = ermineNamespace}) {
    if (annotations == null) {
      return null;
    }
    for (final annotation in annotations) {
      if (annotation.key == AnnotationKey(namespace: namespace, value: name)) {
        return annotation.value.text;
      }
    }
    return null;
  }

  bool _validateViewSpec(ViewSpec viewSpec, String? url) {
    if ((viewSpec.viewHolderToken == null &&
            viewSpec.viewportCreationToken == null) ||
        viewSpec.viewRef == null) {
      log.warning(
          'ViewSpec has null ViewportCreationToken, ViewHolderToken or ViewRef');
      if (url != null) {
        onError(url,
            'ViewSpec has null ViewportCreationToken, ViewHolderToken or ViewRef');
      }
      return false;
    }

    if (viewSpec.viewportCreationToken != null &&
        viewSpec.viewHolderToken != null) {
      log.warning(
          'ViewSpec has both ViewportCreationToken and ViewHolderToken set');
      if (url != null) {
        onError(url,
            'ViewSpec has both ViewportCreationToken and ViewHolderToken set');
      }
      return false;
    }

    return true;
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

  void close([int epitaph = 0]) {
    if (_binding.isBound) {
      _binding.close(epitaph);
    }
    _onPresentedStreamController.close();
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
