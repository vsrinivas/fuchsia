// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: avoid_as

import 'dart:convert' show utf8, json;
import 'dart:typed_data';

import 'package:fidl_fuchsia_mem/fidl_async.dart' as fuchsia_mem;
import 'package:fidl_fuchsia_modular_session/fidl_async.dart';
import 'package:fidl_fuchsia_modular_testing/fidl_async.dart';
import 'package:zircon/zircon.dart';

/// A class which aids in the building of [TestHarnessSpec] objects.
///
/// This class is used to build up the spec and then pass that to the
/// run method of the test harness.
/// ```
/// final builder = TestHarnessBuilder()
///   ..addComponentToIntercept(componentUrl);
/// await harness.run(builder.build());
/// ```
class TestHarnessSpecBuilder {
  BasemgrConfig? _basemgrConfig = BasemgrConfig();
  SessionmgrConfig _sessionmgrConfig = SessionmgrConfig();
  final _componentsToIntercept = <InterceptSpec>[];
  final _envServicesToInherit = <String>['fuchsia.logger.LogSink'];
  final _envComponentServices = <ComponentService>[];

  /// Add a non default basemgr configuration.
  void setBasemgrConfig(BasemgrConfig? basemgrConfig) {
    ArgumentError.checkNotNull(basemgrConfig, 'basemgrConfig');
    _basemgrConfig = basemgrConfig!;
  }

  /// Add a non default sessionmgr configuration.
  void setSessionmgrConfig(SessionmgrConfig? sessionmgrConfig) {
    ArgumentError.checkNotNull(sessionmgrConfig, 'sessionmgrConfig');
    _sessionmgrConfig = sessionmgrConfig!;
  }

  /// Registers the component url to be intercepted.
  ///
  /// When a component with the given [componentUrl] is launched inside the
  /// hermetic environment it will not be launched by the system but rather
  /// passed to the [TestHarness]'s onNewComponent stream.
  ///
  /// Optionally, additional [services] can be provided which will be added
  /// to the intercepted components cmx file.
  void addComponentToIntercept(String? componentUrl, {List<String>? services}) {
    ArgumentError.checkNotNull(componentUrl, 'componentUrl');

    if (componentUrl!.isEmpty) {
      throw ArgumentError('componentUrl must not be an empty string');
    }

    // verify that we have unique component urls
    for (final spec in _componentsToIntercept) {
      if (spec.componentUrl == componentUrl) {
        throw Exception(
            'Attempting to add [$componentUrl] twice. Component urls must be unique');
      }
    }

    final extraContents = <String, dynamic>{};
    if (services != null) {
      extraContents['services'] = services;
    }
    _componentsToIntercept.add(InterceptSpec(
        componentUrl: componentUrl,
        extraCmxContents: _createCmxSandBox(extraContents)));
  }

  /// Adds [service] to the list of services which should be inherited by the
  /// parent environment.
  ///
  /// The following environemnt services will be added by default. If you wish
  /// to not inherit these services you can remove them after building.
  ///  - fuchsia.logger.LogSink
  void addEnvironmentServiceToInherit(String? service) {
    ArgumentError.checkNotNull(service, 'service');

    if (service!.isEmpty) {
      throw ArgumentError('service must not be an empty string');
    }

    if (_envServicesToInherit.contains(service)) {
      throw Exception(
          'Attempting to add [$service] twice. Services must be unique');
    }

    _envServicesToInherit.add(service);
  }

  /// Adds the service with the given [name] and [componentUrl] to the list
  /// of services to inject.
  ///
  /// The [TestHarness] will automatically create these services and add
  /// expose them to the environment under test.
  void addServiceFromComponent(String? name, String? componentUrl) {
    ArgumentError.checkNotNull(name, 'name');
    ArgumentError.checkNotNull(componentUrl, 'componentUrl');

    if (name!.isEmpty || componentUrl!.isEmpty) {
      throw ArgumentError('name and componentUrl must not be an empty string');
    }

    final service = ComponentService(name: name, url: componentUrl);
    if (_envComponentServices.contains(service)) {
      throw Exception(
          'Attempting to add [$service] twice. Services must be unique');
    }

    _envComponentServices.add(service);
  }

  /// Returns the [TestHarnessSpec] object which can be passed to the [TestHarnessProxy]
  ///
  /// After building the [TestHarnessSpec] you may update its values directly if the builder
  /// does not offer a method which satisfies your specific needs.
  TestHarnessSpec build() {
    return TestHarnessSpec(
        basemgrConfig: _basemgrConfig,
        sessionmgrConfig: _sessionmgrConfig,
        componentsToIntercept: _componentsToIntercept,
        envServicesToInherit: _envServicesToInherit,
        envServices: EnvironmentServicesSpec(
          servicesFromComponents: _envComponentServices,
        ));
  }

  fuchsia_mem.Buffer? _createCmxSandBox(Map<String, dynamic> contents) {
    if (contents.isEmpty) {
      return null;
    }
    final encodedContents = utf8.encode(json.encode({'sandbox': contents}));

    final vmo = SizedVmo.fromUint8List(encodedContents as Uint8List);
    return fuchsia_mem.Buffer(vmo: vmo, size: encodedContents.length);
  }
}
