// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_element/fidl_async.dart';
import 'package:fuchsia_services/services.dart';

typedef ControllerClosedCallback = void Function(String id);

/// Namespace for annotations set by ermine.
const String ermineNamespace = 'ermine';

/// Namespace for annotations set by [ElementManager].
const String elementManagerNamespace = 'element_manager';

/// Defines a service to launch applications given their [url] using the
/// [ElementManager.proposeElement] API.
class LaunchService {
  late final ControllerClosedCallback onControllerClosed;

  Future<ControllerProxy> launch(String title, String url,
      {String? alternateServiceName}) async {
    final elementController = ControllerProxy();
    final proxy = ManagerProxy();

    final incoming = Incoming.fromSvcPath();
    if (alternateServiceName != null) {
      incoming.connectToServiceByNameWithChannel(
          alternateServiceName, proxy.ctrl.request().passChannel());
    } else {
      incoming.connectToService(proxy);
    }

    final id = '${DateTime.now().millisecondsSinceEpoch}';
    final annotations = [
      Annotation(
        key: AnnotationKey(namespace: ermineNamespace, value: 'id'),
        value: AnnotationValue.withText(id),
      ),
      Annotation(
        key: AnnotationKey(namespace: elementManagerNamespace, value: 'url'),
        value: AnnotationValue.withText(url),
      ),
      Annotation(
        key: AnnotationKey(namespace: ermineNamespace, value: 'name'),
        value: AnnotationValue.withText(title),
      ),
    ];

    final spec = Spec(componentUrl: url, annotations: annotations);
    await proxy.proposeElement(spec, elementController.ctrl.request());

    proxy.ctrl.close();
    await incoming.close();

    // ignore: unawaited_futures
    elementController.ctrl.whenClosed.then((_) => onControllerClosed(id));

    return elementController;
  }
}
