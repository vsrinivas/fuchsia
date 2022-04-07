// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl/fidl.dart' as fidl;
import 'package:fidl_fuchsia_component_runner/fidl_async.dart' as fcrunner;
import 'package:fidl_fuchsia_io/fidl_async.dart' as fio;

import 'internal/local_component.dart';

// The name of the default instance of a Fuchsia component `Service`.
const _defaultServiceInstance = 'default';

/// A ServiceProxy extends FIDL [DirectoryProxy] to open service connections to
/// instances of the given member protocol. A [LocalComponent] can connect
/// to services using its given [LocalComponentHandles].
class ServiceProxy<P extends fidl.AsyncProxy> extends fio.DirectoryProxy {
  ServiceProxy();

  /// Opens a member protocol of a FIDL service, using the given protocol
  /// proxy. The proxy is connected and then returned so it can be called.
  P openMember(String member, P serviceMemberProxy) {
    open(
      fio.OpenFlags.rightReadable | fio.OpenFlags.rightWritable,
      fio.modeTypeService,
      member,
      fidl.InterfaceRequest<fio.Node>(
          serviceMemberProxy.ctrl.request().passChannel()!),
    );
    return serviceMemberProxy;
  }
}

/// The handles from the framework over which the local component should
/// interact with other components.
class LocalComponentHandles {
  final fcrunner.ComponentControllerBinding controllerBinding;

  final namespaceDirs = <String, fio.DirectoryProxy>{};

  /// The outgoing directory handle for a local component. This can be used to
  /// run a [ServiceFs] for the component.
  final fidl.InterfaceRequest<fio.Directory> outgoingDir;

  LocalComponentHandles(
    this.controllerBinding,
    List<fcrunner.ComponentNamespaceEntry> namespace,
    this.outgoingDir,
  ) {
    for (final namespaceEntry in namespace) {
      final path = namespaceEntry.path;
      if (path == null) {
        throw Exception('namespace entry missing path');
      }
      final directory = namespaceEntry.directory;
      if (directory == null) {
        throw Exception('namespace entry missing directory handle');
      }
      final proxy = fio.DirectoryProxy();
      proxy.ctrl.bind(directory);
      namespaceDirs[path] = proxy;
    }
  }

  /// Connects the given FIDL proxy to the exposed service, using its default
  /// protocol name, and returns the proxy.
  P connectToProtocol<P extends fidl.AsyncProxy>(P proxy) {
    final ctrl = proxy.ctrl;
    final protocolName = ctrl.$serviceName ?? ctrl.$interfaceName!;
    return connectToNamedProtocol(protocolName, proxy);
  }

  /// Connects the given FIDL proxy to the exposed service, using the given
  /// [protocolName], and returns the proxy.
  P connectToNamedProtocol<P extends fidl.AsyncProxy>(
    String protocolName,
    P proxy,
  ) {
    final svcDirProxy = namespaceDirs['/svc'];
    if (svcDirProxy == null) {
      throw Exception(
          "the component's namespace doesn't have an /svc directory");
    }
    svcDirProxy.open(
      fio.OpenFlags.rightReadable | fio.OpenFlags.rightWritable,
      fio.modeTypeService,
      protocolName,
      fidl.InterfaceRequest<fio.Node>(proxy.ctrl.request().passChannel()!),
    );
    return proxy;
  }

  /// Opens a FIDL service with the given name as a directory, which holds
  /// instances of the service.
  fio.DirectoryProxy openNamedService(String serviceName) {
    final svcDirProxy = namespaceDirs['/svc'];
    if (svcDirProxy == null) {
      throw Exception(
          "the component's namespace doesn't have an /svc directory");
    }
    final serviceDir = fio.DirectoryProxy();
    svcDirProxy.open(
      fio.OpenFlags.directory |
          fio.OpenFlags.rightReadable |
          fio.OpenFlags.rightWritable,
      fio.modeTypeDirectory,
      serviceName,
      fidl.InterfaceRequest<fio.Node>(serviceDir.ctrl.request().passChannel()!),
    );
    return serviceDir;
  }

  /// Connects the given [ServiceProxy] to the exposed service, using the
  /// default [serviceName] and the instance name "default", and returns the
  /// proxy.
  S connectToService<S extends ServiceProxy>(S serviceProxy) {
    return connectToServiceInstance(_defaultServiceInstance, serviceProxy);
  }

  /// Connects the given [ServiceProxy] to the exposed service, using the
  /// default [serviceName] and given [instanceName], and returns the proxy.
  /// Connects the given [ServiceProxy] to the exposed service, using the
  /// default [serviceName] in the component's `/svc` directory, and given
  /// [instanceName], and returns the proxy. [instanceName] is a path of one or
  /// more components.
  S connectToServiceInstance<S extends ServiceProxy>(
      String instanceName, S serviceProxy) {
    final ctrl = serviceProxy.ctrl;
    final serviceName = ctrl.$serviceName ?? ctrl.$interfaceName!;
    return connectToNamedServiceInstance(
        serviceName, _defaultServiceInstance, serviceProxy);
  }

  /// Connects the given [ServiceProxy] to the exposed service, using the given
  /// [serviceName] in the component's `/svc` directory, and given
  /// [instanceName], and returns the proxy. [instanceName] is a path of one or
  /// more components.
  S connectToNamedServiceInstance<S extends ServiceProxy>(
    String serviceName,
    String instanceName,
    S serviceProxy,
  ) {
    openNamedService(serviceName).open(
      fio.OpenFlags.rightReadable | fio.OpenFlags.rightWritable,
      fio.modeTypeService,
      instanceName,
      fidl.InterfaceRequest<fio.Node>(
          serviceProxy.ctrl.request().passChannel()!),
    );
    return serviceProxy;
  }

  /// Clones a directory from the local component's namespace.
  ///
  /// Note that this function only works on exact matches from the namespace.
  /// For example if the namespace had a `data` entry in it, and the caller
  /// wished to open the subdirectory at `data/assets`, then this function
  /// should be called with the argument `data` and the returned
  /// `DirectoryProxy` would then be used to open the subdirectory `assets`. In
  /// this scenario, passing `data/assets` in its entirety to this function
  /// would fail.
  ///
  /// ```
  /// final dataDir = handles.cloneFromNamespace("data");
  /// final assetsDir = fio.DirectoryProxy();
  /// dataDir.open(
  ///   fio.OpenFlags.directory |
  ///       fio.OpenFlags.rightReadable |
  ///       fio.OpenFlags.rightWritable,
  ///   fio.modeTypeDirectory,
  ///   "assets",
  ///   fidl.InterfaceRequest<fio.Node>(assetsDir.ctrl.request().passChannel()!),
  /// );
  /// ```
  fio.DirectoryProxy cloneFromNamespace(String directoryName) {
    final dirProxy = namespaceDirs['/$directoryName'];
    if (dirProxy == null) {
      throw Exception(
        "the local component's namespace doesn't have a "
        '/$directoryName directory',
      );
    }
    final clonedDirProxy = fio.DirectoryProxy();
    dirProxy.clone(
        fio.OpenFlags.cloneSameRights,
        fidl.InterfaceRequest<fio.Node>(
            clonedDirProxy.ctrl.request().passChannel()!));
    return dirProxy;
  }

  void close() {
    controllerBinding.close();
    for (final dir in namespaceDirs.values) {
      dir.close();
    }
    namespaceDirs.clear();
    outgoingDir.close();
  }
}
