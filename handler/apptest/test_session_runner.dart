// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:handler/graph/session_graph.dart';
import 'package:handler/handler.dart';
import 'package:handler/module_instance.dart';
import 'package:handler/module_runner.dart';
import 'package:handler/session.dart';
import 'package:modular_core/log.dart';
import 'package:modular/modular/module.mojom.dart' as module;
import 'package:mojo/core.dart';
import 'package:parser/manifest.dart';
import 'package:parser/recipe.dart';

import '../mojo/module_runner.dart';

/// Runs the provided [recipe] with the given [modules], as described by their
/// [manifests] using [graph] as the data source. [_changeCallback] is executed
/// each time a push is made by a module. Once a session has run, it must be
/// closed in order to clean Mojo handles.
///
/// Each manifest in [manifests] should correspond to one module in
/// [modules]. The url value of each manifest is used as the key to find the
/// corresponding module implementation in [modules]. TODO(etiennej): improve to
/// allow multiple instance of a single module to run in the same session.
class TestSessionRunner {
  final Recipe recipe;
  final SessionGraph graph;
  final Map<Uri, module.Module> modules;
  final List<Manifest> manifests;
  final CloseModuleProxy closeProxyCallback;
  final ComposeModule composeModuleCallback;

  Function _closer;
  Handler _handler;
  Session _session;

  TestSessionRunner(this.recipe, this.graph, this.modules, this.manifests,
      {this.composeModuleCallback, this.closeProxyCallback});

  Session get session => _session;

  void start() {
    assert(_closer == null);
    final Map<Uri, module.ModuleProxy> proxies = <Uri, module.ModuleProxy>{};
    final Map<Uri, module.ModuleStub> stubs = <Uri, module.ModuleStub>{};

    modules.forEach((final Uri uri, final module.Module moduleImpl) {
      final MojoMessagePipe pipe = new MojoMessagePipe();
      final module.ModuleProxy moduleProxy =
          new module.ModuleProxy.fromEndpoint(pipe.endpoints[0]);
      moduleProxy.ctrl.errorFuture.catchError((final dynamic e) {
        log("moduleProxy").info("Error in module $uri: $e");
      });
      final module.ModuleStub moduleStub =
          new module.ModuleStub.fromEndpoint(pipe.endpoints[1])
            ..impl = moduleImpl;
      proxies[uri] = moduleProxy;
      stubs[uri] = moduleStub;
    });

    final ModuleProxyFactory proxyFactory =
        (final ModuleInstance instance) => proxies[instance.manifest.url];

    final List<MojoModuleRunner> moduleRunners = <MojoModuleRunner>[];

    final ModuleRunnerFactory runnerFactory = () {
      final MojoModuleRunner moduleRunner = new MojoModuleRunner(
          proxyFactory, closeProxyCallback,
          composeModuleCallback: composeModuleCallback);
      moduleRunners.add(moduleRunner);
      return moduleRunner;
    };

    _closer = () {
      stubs.values.forEach((final module.ModuleStub stub) {
        stub.close();
      });
      moduleRunners.forEach((final MojoModuleRunner runner) {
        runner.stop();
      });
    };

    _handler = new Handler(manifests: manifests, runnerFactory: runnerFactory);
    _session = new Session.fromRecipe(
        recipe: recipe,
        graph: graph,
        handler: _handler,
        runnerFactory: runnerFactory);

    _session.start();
    // From now on, the execution is driven by module runners. The handler is
    // called back from the module runner whenever it receives an update from
    // the module app.
  }

  void close() {
    assert(_closer != null);
    _closer();
  }
}
