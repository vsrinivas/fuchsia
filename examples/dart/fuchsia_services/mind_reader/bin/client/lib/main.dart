// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';

import 'package:fidl_fuchsia_services_examples/fidl_async.dart';
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:fuchsia/fuchsia.dart' show exit;
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
import 'package:pedantic/pedantic.dart';

import 'src/_thought_leaker_impl.dart';

/// The URL which will be used to launch the mind reader server component.
const _mindReaderServerUrl =
    'fuchsia-pkg://fuchsia.com/mind-reader-dart#meta/mind_reader_server.cmx';

/// The main entry point for the mind reader client
///
/// Optional args:
///   --thought [string]: Exposes the string to the mind_reader server.
void main(List<String> args) {
  setupLogger(name: 'mind_reader_client');

  final thoughtToExpose = _parseArgs(args);

  _run(thoughtToExpose).then((_) => _shutdown());
}

Future<void> _run(String thoughtToExpose) async {
  ServiceList serviceList;

  if (thoughtToExpose == null) {
    log
      ..info(
          'No thought provided. Mind reader will not be able to read your mind.')
      ..info(
          'To expose a thought add `--thought "Fuchsia is cool"` when launching the component');
  } else {
    final thoughtLeaker = ThoughtLeakerImpl(thoughtToExpose);
    serviceList = _makeServiceList(thoughtLeaker);

    log.info('Exposing ThoughtLeakerImpl with the thought "$thoughtToExpose"');
  }

  /// An [Incoming] is a helper class which will allow for connecting to
  /// services exposed by the launched component.
  /// This client component is looking for service under /in/svc/
  /// directory to connect to while the server exposes services others can
  /// connect to under /out/public directory.
  final incoming = Incoming();

  /// The [LaunchInfo] struct is used to construct the component we want to
  /// launch.
  final launchInfo = LaunchInfo(
    url: _mindReaderServerUrl,
    // The directoryRequest is the handle to the /out directory of the launched
    // component.
    directoryRequest: incoming.request().passChannel(),

    // The service list is a list of services which are exposed to the child.
    // If a service is not included in this list the child will fail to connect.
    additionalServices: serviceList,
  );

  final launcherProxy = LauncherProxy();
  unawaited((Incoming.fromSvcPath()..connectToService(launcherProxy)).close());

  // Launch the component and wait for it to start. We pass a
  // [ComponentControllerProxy] here to tie the lifecycle of our component to
  // the one we are launching. If we were to pass null the launched process
  // would live on after this process dies. Note: you can keep a reference to
  // the proxy and close it when you want if your process was longer lived.
  await launcherProxy.createComponent(
      launchInfo, ComponentControllerProxy().ctrl.request());

  // Now that the component has launched we attempt to connect to the mind
  // reader service which is exposed to us by the child.
  final mindReader = MindReaderProxy();
  incoming.connectToService(mindReader);

  await incoming.close();

  // We ask the service to read our mind and wait for the response.
  final response = await mindReader.readMind();
  log.info('Got response: $response');
}

/// Creates a [ServiceList] object which will be used by the launcher to expose
/// the thoughtLeaker service to the launched component.
ServiceList _makeServiceList(ThoughtLeakerImpl thoughtLeaker) {
  // Create a [ServiceProvider] and expose the service to the child. This service
  // will be exposed to the child in the in/svc directory. When the child attempts
  // to connect to the service the [bind] method will be called with the
  // given request.
  final provider = ServiceProviderImpl()
    ..addServiceForName(thoughtLeaker.bind, ThoughtLeakerImpl.serviceName);

  // The binding will forward the messages sent to the channel to the provided
  // thoughtLeaker implementation. The [wrap] method on the binding will create
  // a bound channel which will forward messages.
  final serviceProviderBinding = ServiceProviderBinding();
  final serviceList = ServiceList(
    names: [ThoughtLeaker.$serviceName],
    provider: serviceProviderBinding.wrap(provider),
  );

  return serviceList;
}

/// Attempts to parse the args for a supplied thought. If no thought is provided
/// null will be returned.
String _parseArgs(List<String> args) {
  if (args.length >= 2 && args[0] == '--thought') {
    // temporary workaround for not being able to group args with quotes
    return args[1];
  }
  return null;
}

void _shutdown() {
  // we want to allow the log messages to get piped through to the syslogger
  // before we kill our process.
  Future(() => exit(0));
}
