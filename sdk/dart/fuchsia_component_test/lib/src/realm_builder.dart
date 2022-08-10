// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:typed_data';
import 'dart:math';

import 'package:collection/collection.dart';
import 'package:fidl/fidl.dart' as fidl;
import 'package:fidl_fuchsia_component/fidl_async.dart' as fcomponent;
import 'package:fidl_fuchsia_component_config/fidl_async.dart' as fconfig;
import 'package:fidl_fuchsia_component_decl/fidl_async.dart' as fdecl;
import 'package:fidl_fuchsia_diagnostics_types/fidl_async.dart' as fdiagtypes;
import 'package:fidl_fuchsia_component_test/fidl_async.dart' as ftest;
import 'package:fidl_fuchsia_io/fidl_async.dart' as fio;

import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart' as services;
import 'package:zircon/zircon.dart';

import 'error.dart';
import 'local_component_handles.dart';
import 'internal/local_component.dart';
import 'internal/local_component_runner.dart';

/// The default name of the child component collection that contains built
/// topologies.
const defaultCollectionName = 'realm_builder';

const realmBuilderServerChildName = 'realm_builder_server';

fconfig.ValueSpec _singleValue(fconfig.SingleValue value) {
  return fconfig.ValueSpec(value: fconfig.Value.withSingle(value));
}

fconfig.ValueSpec _vectorValue(fconfig.VectorValue value) {
  return fconfig.ValueSpec(value: fconfig.Value.withVector(value));
}

/// The properties for a child being added to a realm
class ChildOptions {
  fdecl.StartupMode _startup;
  String? environment;
  fdecl.OnTerminate _onTerminate;

  ChildOptions()
      : _startup = fdecl.StartupMode.lazy,
        _onTerminate = fdecl.OnTerminate.none;

  fdecl.StartupMode get startup => _startup;

  void eager() {
    _startup = fdecl.StartupMode.eager;
  }

  fdecl.OnTerminate get onTerminate => _onTerminate;

  void rebootOnTerminate() {
    _onTerminate = fdecl.OnTerminate.reboot;
  }

  /// Convert the current value into the FIDL table-backed ChildOptions, via its
  /// `const` constructor.
  ftest.ChildOptions toFidlType() {
    return ftest.ChildOptions(
      startup: _startup,
      environment: environment,
      onTerminate: _onTerminate,
    );
  }

  @override
  String toString() {
    return 'ChildOptions(startup = $_startup, environment = $environment, '
        'onTerminate = $onTerminate)';
  }
}

/// Provides convenience functions for using the instance. Components v2 only.
class ScopedInstanceFactory {
  fcomponent.RealmProxy? realmProxy;
  final String _collectionName;

  ScopedInstanceFactory(this._collectionName);

  String get collectionName => _collectionName;

  Future<ScopedInstance> newInstance(String url) {
    final id = Random().nextInt(1 << 32);
    final childName = 'auto-${id.toRadixString(16)}';
    return newNamedInstance(childName, url);
  }

  Future<ScopedInstance> newNamedInstance(String childName, String url) async {
    late final fcomponent.RealmProxy realmProxy;
    if (this.realmProxy != null) {
      realmProxy = this.realmProxy!;
    } else {
      realmProxy = fcomponent.RealmProxy();
      await (services.Incoming.fromSvcPath()..connectToService(realmProxy))
          .close();
    }

    final collectionRef = fdecl.CollectionRef(name: _collectionName);
    final childDecl = fdecl.Child(
      name: childName,
      url: url,
      startup: fdecl.StartupMode.lazy,
    );
    final childArgs = fcomponent.CreateChildArgs();

    await realmProxy.createChild(
      collectionRef,
      childDecl,
      childArgs,
    );

    final childRef = fdecl.ChildRef(
      name: childName,
      collection: _collectionName,
    );

    final exposedDir = fio.DirectoryProxy();
    await realmProxy.openExposedDir(
      childRef,
      exposedDir.ctrl.request(),
    );

    return ScopedInstance._(
      realmProxy,
      childName,
      _collectionName,
      exposedDir,
    );
  }
}

/// Provides convenience functions for using the instance. Components v2 only.
class ScopedInstance {
  final fcomponent.RealmProxy _realm;
  final String _childName;
  final String _collectionName;
  final fio.DirectoryProxy _exposedDir;

  const ScopedInstance._(
    this._realm,
    this._childName,
    this._collectionName,
    this._exposedDir,
  );

  static Future<ScopedInstance> create({
    required String collectionName,
    required String url,
    String? childName,
  }) {
    final factory = ScopedInstanceFactory(collectionName);
    if (childName == null) {
      return factory.newInstance(url);
    } else {
      return factory.newNamedInstance(childName, url);
    }
  }

  /// Returns a reference to the component's read-only exposed directory.
  fio.DirectoryProxy get exposedDir => _exposedDir;

  String get childName => _childName;
  String get collectionName => _collectionName;

  /// Connect to exposed fuchsia.component.Binder protocol of instance, thus
  /// triggering it to start.
  ///
  /// Note: This will only work if the component exposes this protocol in its
  /// manifest.
  fcomponent.BinderProxy connectToBinder() {
    try {
      return connectToProtocolAtExposedDir(fcomponent.BinderProxy());
    } on Exception catch (err) {
      log.severe('failed to connect to fuchsia.component.Binder: $err');
      rethrow;
    }
  }

  /// Connect to an instance of a FIDL protocol hosted in the component's
  /// exposed directory, based on the type of the given proxy. The given
  /// proxy is returned.
  T connectToProtocolAtExposedDir<T extends fidl.AsyncProxy>(T proxy) {
    services.Incoming.withDirectory(exposedDir).connectToService(proxy);
    return proxy;
  }

  /// Connects to an instance of a FIDL protocol hosted in the component's
  /// exposed directory using the given [serverEnd]. Any proxy of the same
  /// protocol type is also required, in order to get the protocol name.
  void connectRequestToProtocolAtExposedDir<P>(
      fidl.InterfaceRequest<P> serverEnd, fidl.AsyncProxy<P> anyProxy) {
    final ctrl = anyProxy.ctrl;
    final protocolName = ctrl.$serviceName ?? ctrl.$interfaceName!;
    connectToNamedProtocolAtExposedDir(protocolName, serverEnd.passChannel()!);
  }

  /// Connects to an instance of a FIDL protocol called [protocolName] hosted in
  /// the component's exposed directory, using the given [serverEnd].
  void connectToNamedProtocolAtExposedDir(
      String protocolName, Channel serverEnd) {
    services.Incoming.withDirectory(exposedDir)
        .connectToServiceByNameWithChannel(protocolName, serverEnd);
  }

  /// Connects to an instance of a FIDL protocol called [protocolName] hosted at
  /// the absolute path from the exposed directory.
  T connectToProtocolAtPath<T extends fidl.AsyncProxy>(
      T proxy, String protocolPath) {
    var serverEnd = proxy.ctrl.request().passChannel();
    exposedDir.open(
      fio.OpenFlags.rightReadable | fio.OpenFlags.rightWritable,
      fio.modeTypeService,
      protocolPath,
      fidl.InterfaceRequest<fio.Node>(serverEnd),
    );
    return proxy;
  }

  /// Connects to an instance of a FIDL protocol hosted at the protocol name,
  /// in the given directory.
  T connectToProtocolInDirPath<T extends fidl.AsyncProxy>(
    T proxy,
    String dirPath,
  ) {
    return connectToProtocolAtPath(
      proxy,
      '$dirPath/${proxy.ctrl.$serviceName}',
    );
  }

  /// Call [close()] before the [ScopedInstance] goes out of scope (since
  /// Dart doesn't support destructors or RAII destruction when an object
  /// goes out of scope).
  ///
  /// This will ensure that the message goes out to the realm.
  ///
  /// Note: `destroyChild()`s returned Future may not complete immediately.
  /// The process of destroying a component and its children includes various
  /// confirmations, with time-outs in case a component doesn't stop cleanly.
  /// This `close()` method sends the `destroyChild()` request and returns
  /// without waiting for confirmation. It returns `void` instead of the
  /// [Future] so callers are discouraged from awaiting the close(), which can
  /// be the source of non-deterministic delays and/or timeouts.
  void close() {
    _realm.destroyChild(fdecl.ChildRef(
      name: childName,
      collection: collectionName,
    ));
  }
}

enum RefType {
  capability,
  child,
  collection,
  debug,
  framework,
  parent,
  self,
}

extension RefEnumToName on RefType {
  String enumName() {
    final segments = toString().split('.');
    return segments.isEmpty ? '' : segments.last;
  }
}

class Ref {
  final RefType type;

  String? name;

  List<String>? scope;

  Ref.from(Ref o)
      : type = o.type,
        name = o.name,
        scope = o.scope?.toList();

  Ref.capability(this.name) : type = RefType.capability;
  Ref.collection(this.name) : type = RefType.collection;
  Ref.debug() : type = RefType.debug;
  Ref.framework() : type = RefType.framework;
  Ref.parent() : type = RefType.parent;
  Ref.self() : type = RefType.self;

  Ref.child(ChildRef childRef)
      : type = RefType.child,
        name = childRef.name,
        scope = childRef.scope;

  Ref.childFromSubRealm(SubRealmBuilder subRealm)
      : type = RefType.child,
        name = subRealm.realmPath.isEmpty ? null : subRealm.realmPath.last,
        scope = subRealm.realmPath.toList()..removeLast() {
    if (name == null) {
      throw Exception(
          'API bug: It should not be possible to call fromSubRealmBuilder '
          'with a top-level SubRealmBuilder');
    }
  }

  void checkScope(List<String> realmScope) {
    if (scope != null && !ListEquality().equals(scope, realmScope)) {
      throw RefUsedInWrongRealmException(this, realmScope.join('/'));
    }
  }

  @override
  bool operator ==(Object o) =>
      o is Ref &&
      type == o.type &&
      name == o.name &&
      ListEquality().equals(scope, o.scope);

  @override
  int get hashCode => type.hashCode + name.hashCode + scope.hashCode;

  /// Convert the current value into the FIDL table-backed ChildOptions, via its
  /// `const` constructor.
  fdecl.Ref toFidlType() {
    switch (type) {
      case RefType.capability:
        return fdecl.Ref.withCapability(fdecl.CapabilityRef(name: name!));
      case RefType.child:
        return fdecl.Ref.withChild(fdecl.ChildRef(name: name!));
      case RefType.collection:
        return fdecl.Ref.withCollection(fdecl.CollectionRef(name: name!));
      case RefType.debug:
        return fdecl.Ref.withDebug(fdecl.DebugRef());
      case RefType.framework:
        return fdecl.Ref.withFramework(fdecl.FrameworkRef());
      case RefType.parent:
        return fdecl.Ref.withParent(fdecl.ParentRef());
      case RefType.self:
        return fdecl.Ref.withSelf(fdecl.SelfRef());
    }
  }

  @override
  String toString() {
    return '${type.enumName()} Ref(name=${name == null ? 'null' : '"$name"'}, '
        'scope=$scope)';
  }
}

class ChildRef {
  String name;
  List<String>? scope;

  ChildRef(this.name, [this.scope]);

  void checkScope(List<String> realmScope) {
    if (scope != null && !ListEquality().equals(scope, realmScope)) {
      throw RefUsedInWrongRealmException(
        Ref.child(this),
        realmScope.join('/'),
      );
    }
  }

  ChildRef.fromSubRealmBuilder(SubRealmBuilder input)
      : name = input.realmPath.isEmpty ? '' : input.realmPath.last,
        scope = input.realmPath.toList()..removeLast() {
    if (name == '') {
      throw Exception(
          'API bug: It should not be possible to call fromSubRealmBuilder '
          'with a top-level SubRealmBuilder');
    }
  }

  ChildRef.fromChildRef(ChildRef input)
      : name = input.name,
        scope = input.scope?.toList();

  @override
  String toString() {
    return 'ChildRef(name = $name, scope = $scope)';
  }
}

extension DependencyTypeEnumToName on fdecl.DependencyType {
  String enumName() {
    final segments = toString().split('.');
    return segments.isEmpty ? '' : segments.last;
  }
}

abstract class Capability {
  String name;

  /// The name the targets will see the capability as.
  String? as;

  Capability(this.name, [this.as]);

  ftest.Capability toFidlType();

  String _capabilityToString(String? additionalFields) {
    return '$runtimeType(name = $name, '
        'as = $as${additionalFields != null ? ', $additionalFields' : ''})';
  }
}

/// A protocol capability, which may be routed between components. Created by
/// [Capability.protocol()].
class ProtocolCapability extends Capability {
  fdecl.DependencyType type;

  /// The path at which this protocol capability will be provided or used. Only
  /// relevant if the route's source or target is a legacy or local component,
  /// as these are the only components that realm builder will generate a modern
  /// component manifest for.
  String? path;

  ProtocolCapability(
    name, {
    as,
    this.type = fdecl.DependencyType.strong,
    this.path,
  }) : super(name, as);

  ProtocolCapability.from(ProtocolCapability o)
      : type = o.type,
        path = o.path,
        super(o.name, o.as);

  /// Marks any offers involved in this route as "weak", which will cause this
  /// route to be ignored when determining shutdown ordering.
  void weak() {
    type = fdecl.DependencyType.weak;
  }

  @override
  ftest.Capability toFidlType() {
    return ftest.Capability.withProtocol(ftest.Protocol(
      name: name,
      as: as,
      type: type,
      path: path,
    ));
  }

  @override
  bool operator ==(Object o) =>
      o is ProtocolCapability &&
      name == o.name &&
      as == o.as &&
      type == o.type &&
      path == o.path;

  @override
  int get hashCode =>
      name.hashCode + as.hashCode + type.hashCode + path.hashCode;

  @override
  String toString() {
    return _capabilityToString('type = ${type.enumName()}, path = $path');
  }
}

/// A directory capability, which may be routed between components. Created by
/// [Capability.directory()].
class DirectoryCapability extends Capability {
  fdecl.DependencyType type;

  /// The rights the target will be allowed to use when accessing the directory.
  fio.Operations? rights;

  /// The sub-directory of the directory that the target will be given access
  /// to.
  String? subdir;

  /// The path at which this directory will be provided or used. Only relevant
  /// if the route's source or target is a local component.
  String? path;

  DirectoryCapability(
    name, {
    as,
    this.type = fdecl.DependencyType.strong,
    this.rights,
    this.subdir,
    this.path,
  }) : super(name, as);

  DirectoryCapability.from(DirectoryCapability o)
      : type = o.type,
        path = o.path,
        super(o.name, o.as);

  /// Marks any offers involved in this route as "weak", which will cause this
  /// route to be ignored when determining shutdown ordering.
  void weak() {
    type = fdecl.DependencyType.weak;
  }

  @override
  ftest.Capability toFidlType() {
    return ftest.Capability.withDirectory(ftest.Directory(
      name: name,
      as: as,
      type: type,
      rights: rights,
      subdir: subdir,
      path: path,
    ));
  }

  @override
  bool operator ==(Object o) =>
      o is DirectoryCapability &&
      name == o.name &&
      as == o.as &&
      type == o.type &&
      rights == o.rights &&
      subdir == o.subdir &&
      path == o.path;

  @override
  int get hashCode =>
      name.hashCode +
      as.hashCode +
      type.hashCode +
      rights.hashCode +
      subdir.hashCode +
      path.hashCode;

  @override
  String toString() {
    return _capabilityToString(
        'type = ${type.enumName()}, rights = $rights, subdir = $subdir, '
        'path = $path');
  }
}

/// A storage capability, which may be routed between components. Created by
/// [Capability.storage()].
class StorageCapability extends Capability {
  /// The path at which this storage capability will be provided or used. Only
  /// relevant if the route's source or target is a legacy or local component,
  /// as these are the only components that realm builder will generate a modern
  /// component manifest for.
  String? path;

  StorageCapability(name, {as, this.path}) : super(name, as);

  StorageCapability.from(StorageCapability o)
      : path = o.path,
        super(o.name, o.as);

  @override
  ftest.Capability toFidlType() {
    return ftest.Capability.withStorage(
        ftest.Storage(name: name, as: as, path: path));
  }

  @override
  bool operator ==(Object o) =>
      o is StorageCapability && name == o.name && as == o.as && path == o.path;

  @override
  int get hashCode => name.hashCode + as.hashCode + path.hashCode;

  @override
  String toString() {
    return _capabilityToString('path = $path');
  }
}

/// A service capability, which may be routed between components. Created by
/// [Capability.service()].
class ServiceCapability extends Capability {
  /// The path at which this service capability will be provided or used. Only
  /// relevant if the route's source or target is a legacy or local component,
  /// as these are the only components that realm builder will generate a modern
  /// component manifest for.
  String? path;

  ServiceCapability(
    name, {
    as,
    this.path,
  }) : super(name, as);

  ServiceCapability.from(ServiceCapability o)
      : path = o.path,
        super(o.name, o.as);

  @override
  ftest.Capability toFidlType() {
    return ftest.Capability.withService(ftest.Service(
      name: name,
      as: as,
      path: path,
    ));
  }

  @override
  bool operator ==(Object o) =>
      o is ServiceCapability && name == o.name && as == o.as && path == o.path;

  @override
  int get hashCode => name.hashCode + as.hashCode + path.hashCode;

  @override
  String toString() {
    return _capabilityToString('path = $path');
  }
}

/// A route of one or more capabilities from one point in the realm to one or
/// more targets.
class Route {
  final List<Capability> _capabilities;
  Ref? _from;
  final List<Ref> _to;

  Route()
      : _capabilities = [],
        _to = [];

  Route.from(Route o)
      : _capabilities = o._capabilities.toList(),
        _from = o._from,
        _to = o._to.toList();

  List<Capability> getCapabilities() => _capabilities;
  Ref? getFrom() => _from;
  List<Ref> getTo() => _to;

  void capability(Capability capability) {
    _capabilities.add(capability);
  }

  /// Adds a source to this route. Must be called exactly once. Will panic if
  /// called a second time.
  void from(Ref from) {
    if (_from != null) {
      throw Exception('from is already set for this route');
    }
    _from = from;
  }

  void to(Ref to) {
    _to.add(to);
  }

  @override
  bool operator ==(Object o) =>
      o is Route &&
      ListEquality().equals(_capabilities, o._capabilities) &&
      _from == o._from &&
      ListEquality().equals(_to, o._to);

  @override
  int get hashCode => _capabilities.hashCode + _from.hashCode + _to.hashCode;
}

// [START mock_interface_dart]
typedef OnRun = Future<void> Function(
  LocalComponentHandles handles,
  Completer onStop,
);
// [END mock_interface_dart]
typedef OnKill = Future<void> Function(LocalComponentHandles handles);
typedef OnOnPublishDiagnostics = Stream<fdiagtypes.ComponentDiagnostics>
    Function(LocalComponentHandles handles);
typedef OnStop = Future<void> Function(LocalComponentHandles handles);

/// A running instance of a created realm. Important: When a RealmInstance is no
/// longer needed, the root [ScopedInstance] must be closed--by calling
/// root.close()--to ensure the child component is not leaked. When closed, the
/// realm is destroyed, along with any components that were in the realm.
class RealmInstance {
  /// The root component of this realm instance, which can be used to access
  /// exposed capabilities from the realm.
  final ScopedInstance root;

  const RealmInstance(this.root);
}

class SubRealmBuilder {
  ftest.RealmProxy realmProxy;
  List<String> realmPath;

  // Required for builder API but not yet implemented or used.
  final _localComponentRunnerBuilder = LocalComponentRunnerBuilder();

  SubRealmBuilder({
    required this.realmProxy,
    required this.realmPath,
  });

  /// Adds a child realm and returns a [SubRealmBuilder]. Capabilities can be
  /// routed between parent [RealmBuilder] and the sub-realm, and then routed
  /// to/from children of the sub-realm.
  Future<SubRealmBuilder> addChildRealm(String name,
      [ChildOptions? options]) async {
    final childRealm = ftest.RealmProxy();
    await realmProxy.addChildRealm(
      name,
      (options ?? ChildOptions()).toFidlType(),
      childRealm.ctrl.request(),
    );
    final childPath = realmPath.toList()..add(name);
    return SubRealmBuilder(
      realmProxy: childRealm,
      realmPath: childPath,
    );
  }

  /// Adds a local component to the realm. the [onRun] callback will be called
  /// when the component starts. Other [ComponentController] callbacks are
  /// optional.
  ///
  /// The [ComponentController] binding must be closed to indicate to Component
  /// Manager that the [LocalComponent] has stopped. If [onStop] is not
  /// provided, the default implementation will automatically close the
  /// [ComponentController] binding. If [onRun] completes before [onStop] (if
  /// called) the [ComponentController] binding will be closed automatically. If
  /// [onStop] is provided, the caller can close the [ComponentController]
  /// binding by calling [LocalComponentHandles.close()].
  ///
  /// [onRun] is given the [LocalComponentHandles] (to access its namespace,
  /// outgoing directory, and [ComponentController]), and a [Completer] that
  /// is completed if [ComponentController.stop()] is called. The caller can
  /// await the `onStopCompleter` allow the component to continue to remain
  /// active until the `stop()` is received.
  Future<ChildRef> addLocalChild(
    String name, {
    required OnRun onRun,
    OnKill? onKill,
    OnOnPublishDiagnostics? onOnPublishDiagnostics,
    OnStop? onStop,
    ChildOptions? options,
  }) async {
    await realmProxy.addLocalChild(
      name,
      (options ?? ChildOptions()).toFidlType(),
    );
    final childPath = realmPath + [name];
    _localComponentRunnerBuilder.registerLocalComponent(
      LocalComponent(
        childPath.join('/'),
        onRun,
        onKill,
        onOnPublishDiagnostics,
        onStop,
      ),
    );
    return ChildRef(name, realmPath);
  }

  /// Adds a new component to the realm by URL.
  Future<ChildRef> addChild(String name, String url,
      [ChildOptions? options]) async {
    await realmProxy.addChild(
      name,
      url,
      (options ?? ChildOptions()).toFidlType(),
    );
    return ChildRef(name, realmPath);
  }

  /// Adds a new legacy component to the realm.
  Future<ChildRef> addLegacyChild(String name, String legacyUrl,
      [ChildOptions? options]) async {
    await realmProxy.addLegacyChild(
      name,
      legacyUrl,
      (options ?? ChildOptions()).toFidlType(),
    );
    return ChildRef(name, realmPath);
  }

  /// Adds a new component to the realm with the given component declaration
  Future<ChildRef> addChildFromDecl(String name, fdecl.Component decl,
      [ChildOptions? options]) async {
    await realmProxy.addChildFromDecl(
      name,
      decl,
      (options ?? ChildOptions()).toFidlType(),
    );
    return ChildRef(name, realmPath);
  }

  /// Returns a copy the decl for a child in this realm
  Future<fdecl.Component> getComponentDecl(ChildRef childRef) {
    childRef.checkScope(realmPath);
    return realmProxy.getComponentDecl(childRef.name);
  }

  /// Replaces the decl for a child of this realm
  Future<void> replaceComponentDecl(
    ChildRef childRef,
    fdecl.Component decl,
  ) {
    childRef.checkScope(realmPath);
    return realmProxy.replaceComponentDecl(childRef.name, decl);
  }

  /// Returns a copy the decl for a child in this realm
  Future<fdecl.Component> getRealmDecl() {
    return realmProxy.getRealmDecl();
  }

  /// Replaces the decl for this realm
  Future<void> replaceRealmDecl(fdecl.Component decl) {
    return realmProxy.replaceRealmDecl(decl);
  }

  /// Load the component's packaged config if available.
  Future<void> initMutableConfigFromPackage(
    ChildRef childRef,
  ) {
    childRef.checkScope(realmPath);
    return realmProxy.initMutableConfigFromPackage(childRef.name);
  }

  /// Allow setting config values without loading packaged config.
  Future<void> initMutableConfigToEmpty(
    ChildRef childRef,
  ) {
    childRef.checkScope(realmPath);
    return realmProxy.initMutableConfigToEmpty(childRef.name);
  }

  /// Replaces a value of a given configuration field
  Future<void> setConfigValue(
    ChildRef childRef,
    String key,
    fconfig.ValueSpec value,
  ) {
    childRef.checkScope(realmPath);
    // TODO(https://fxbug.dev/103951) switch to setConfigValue
    return realmProxy.replaceConfigValue(childRef.name, key, value);
  }

  /// Replaces a boolean value of a given configuration field
  Future<void> setConfigValueBool(
    ChildRef childRef,
    String key,
    // ignore: avoid_positional_boolean_parameters
    bool value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _singleValue(fconfig.SingleValue.withBool(value)),
    );
  }

  /// Replaces a uint8 value of a given configuration field
  Future<void> setConfigValueUint8(
    ChildRef childRef,
    String key,
    int value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _singleValue(fconfig.SingleValue.withUint8(value)),
    );
  }

  /// Replaces a uint16 value of a given configuration field
  Future<void> setConfigValueUint16(
    ChildRef childRef,
    String key,
    int value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _singleValue(fconfig.SingleValue.withUint16(value)),
    );
  }

  /// Replaces a uint32 value of a given configuration field
  Future<void> setConfigValueUint32(
    ChildRef childRef,
    String key,
    int value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _singleValue(fconfig.SingleValue.withUint32(value)),
    );
  }

  /// Replaces a uint64 value of a given configuration field
  Future<void> setConfigValueUint64(
    ChildRef childRef,
    String key,
    int value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _singleValue(fconfig.SingleValue.withUint64(value)),
    );
  }

  /// Replaces a int8 value of a given configuration field
  Future<void> setConfigValueInt8(
    ChildRef childRef,
    String key,
    int value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _singleValue(fconfig.SingleValue.withInt8(value)),
    );
  }

  /// Replaces a int16 value of a given configuration field
  Future<void> setConfigValueInt16(
    ChildRef childRef,
    String key,
    int value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _singleValue(fconfig.SingleValue.withInt16(value)),
    );
  }

  /// Replaces a int32 value of a given configuration field
  Future<void> setConfigValueInt32(
    ChildRef childRef,
    String key,
    int value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _singleValue(fconfig.SingleValue.withInt32(value)),
    );
  }

  /// Replaces a int64 value of a given configuration field
  Future<void> setConfigValueInt64(
    ChildRef childRef,
    String key,
    int value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _singleValue(fconfig.SingleValue.withInt64(value)),
    );
  }

  /// Replaces a string value of a given configuration field
  Future<void> setConfigValueString(
    ChildRef childRef,
    String key,
    String value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _singleValue(fconfig.SingleValue.withString$(value)),
    );
  }

  /// Replaces a boolean vector value of a given configuration field
  Future<void> setConfigValueBoolVector(
    ChildRef childRef,
    String key,
    List<bool> value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _vectorValue(fconfig.VectorValue.withBoolVector(value)),
    );
  }

  /// Replaces a uint8 vector value of a given configuration field
  Future<void> setConfigValueUint8Vector(
    ChildRef childRef,
    String key,
    Uint8List value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _vectorValue(fconfig.VectorValue.withUint8Vector(value)),
    );
  }

  /// Replaces a uint16 vector value of a given configuration field
  Future<void> setConfigValueUint16Vector(
    ChildRef childRef,
    String key,
    Uint16List value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _vectorValue(fconfig.VectorValue.withUint16Vector(value)),
    );
  }

  /// Replaces a uint32 vector value of a given configuration field
  Future<void> setConfigValueUint32Vector(
    ChildRef childRef,
    String key,
    Uint32List value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _vectorValue(fconfig.VectorValue.withUint32Vector(value)),
    );
  }

  /// Replaces a uint64 vector value of a given configuration field
  Future<void> setConfigValueUint64Vector(
    ChildRef childRef,
    String key,
    Uint64List value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _vectorValue(fconfig.VectorValue.withUint64Vector(value)),
    );
  }

  /// Replaces a int8 vector value of a given configuration field
  Future<void> setConfigValueInt8Vector(
    ChildRef childRef,
    String key,
    Int8List value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _vectorValue(fconfig.VectorValue.withInt8Vector(value)),
    );
  }

  /// Replaces a int16 vector value of a given configuration field
  Future<void> setConfigValueInt16Vector(
    ChildRef childRef,
    String key,
    Int16List value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _vectorValue(fconfig.VectorValue.withInt16Vector(value)),
    );
  }

  /// Replaces a int32 vector value of a given configuration field
  Future<void> setConfigValueInt32Vector(
    ChildRef childRef,
    String key,
    Int32List value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _vectorValue(fconfig.VectorValue.withInt32Vector(value)),
    );
  }

  /// Replaces a int64 vector value of a given configuration field
  Future<void> setConfigValueInt64Vector(
    ChildRef childRef,
    String key,
    Int64List value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _vectorValue(fconfig.VectorValue.withInt64Vector(value)),
    );
  }

  /// Replaces a string vector value of a given configuration field
  Future<void> setConfigValueStringVector(
    ChildRef childRef,
    String key,
    List<String> value,
  ) {
    return setConfigValue(
      childRef,
      key,
      _vectorValue(fconfig.VectorValue.withStringVector(value)),
    );
  }

  /// Adds a route between components within the realm
  Future<void> addRoute(Route route) async {
    final capabilities = route.getCapabilities();
    final from = route.getFrom();
    final to = route.getTo();
    if (from == null) {
      throw MissingSource();
    }
    final source = from..checkScope(realmPath);
    for (final target in to) {
      target.checkScope(realmPath);
    }
    if (capabilities.isNotEmpty) {
      await realmProxy.addRoute(
        capabilities.map((c) => c.toFidlType()).toList(),
        source.toFidlType(),
        to.map((Ref ref) => ref.toFidlType()).toList(),
      );
    }
  }
}

/// A class that aids in the building of [RealmBuilder] objects.
///
/// This class is used to create Fuchsia component tests in Dart, using the
/// RealmBuilder framework.
///
/// ```
/// final builder = RealmBuilder();
/// builder.addChild(...);
/// builder.addRoute(...);
/// realmInstance = await builder.build();
/// realmInstance.root.connectToProtocol...
/// ```
class RealmBuilder {
  late final SubRealmBuilder _rootRealm;

  late final ftest.BuilderProxy _builder;

  /// The realm will be launched in the collection named [collectionName].
  final String collectionName;

  /// Creates a new RealmBuilder.
  /// [relativeUrl]: The path to a manifest to load into the realm.
  /// [collectionName]: The collection to add the realm to, when launched.
  static Future<RealmBuilder> create({
    String? relativeUrl,
    String collectionName = defaultCollectionName,
  }) async {
    final componentRealm = fcomponent.RealmProxy();
    await (services.Incoming.fromSvcPath()..connectToService(componentRealm))
        .close();

    final exposedDir = fio.DirectoryProxy();
    await componentRealm.openExposedDir(
      fdecl.ChildRef(name: realmBuilderServerChildName),
      exposedDir.ctrl.request(),
    );

    final realmBuilderFactory = ftest.RealmBuilderFactoryProxy();
    await (services.Incoming.withDirectory(exposedDir)
          ..connectToService(realmBuilderFactory))
        .close();

    final pkgDirHandle =
        fidl.InterfaceHandle<fio.Directory>(Channel.fromFile('/pkg'));

    final realm = ftest.RealmProxy();
    final builder = ftest.BuilderProxy();
    if (relativeUrl == null) {
      await realmBuilderFactory.create(
        pkgDirHandle,
        realm.ctrl.request(),
        builder.ctrl.request(),
      );
    } else {
      // load the manifest
      await realmBuilderFactory.createFromRelativeUrl(
        pkgDirHandle,
        relativeUrl,
        realm.ctrl.request(),
        builder.ctrl.request(),
      );
    }
    return RealmBuilder._(realm, builder, collectionName);
  }

  RealmBuilder._(
    ftest.RealmProxy realmProxy,
    this._builder,
    this.collectionName,
  ) {
    _rootRealm = SubRealmBuilder(realmProxy: realmProxy, realmPath: []);
  }

  SubRealmBuilder get rootRealm => _rootRealm;

  ftest.BuilderProxy get builder => _builder;

  /// Adds a child realm and returns a [SubRealmBuilder]. Capabilities can be
  /// routed between parent [RealmBuilder] and the sub-realm, and then routed
  /// to/from children of the sub-realm.
  Future<SubRealmBuilder> addChildRealm(String name, [ChildOptions? options]) {
    return rootRealm.addChildRealm(
      name,
      options ?? ChildOptions(),
    );
  }

  /// Adds a new component to the realm by URL.
  Future<ChildRef> addLocalChild(
    String name, {
    required OnRun onRun,
    OnKill? onKill,
    OnOnPublishDiagnostics? onOnPublishDiagnostics,
    OnStop? onStop,
    ChildOptions? options,
  }) {
    return rootRealm.addLocalChild(
      name,
      onRun: onRun,
      onKill: onKill,
      onOnPublishDiagnostics: onOnPublishDiagnostics,
      onStop: onStop,
      options: options ?? ChildOptions(),
    );
  }

  /// Adds a new component to the realm by URL.
  Future<ChildRef> addChild(String name, String url, [ChildOptions? options]) {
    return rootRealm.addChild(
      name,
      url,
      options ?? ChildOptions(),
    );
  }

  /// Adds a new legacy component to the realm.
  Future<ChildRef> addLegacyChild(String name, String legacyUrl,
      [ChildOptions? options]) {
    return rootRealm.addLegacyChild(
      name,
      legacyUrl,
      options ?? ChildOptions(),
    );
  }

  /// Adds a new component to the realm with the given component declaration
  Future<ChildRef> addChildFromDecl(String name, fdecl.Component decl,
      [ChildOptions? options]) {
    return rootRealm.addChildFromDecl(
      name,
      decl,
      options ?? ChildOptions(),
    );
  }

  /// Returns a copy the decl for a child in this realm
  Future<fdecl.Component> getComponentDecl(ChildRef childRef) {
    return rootRealm.getComponentDecl(childRef);
  }

  /// Replaces the decl for a child of this realm
  Future<void> replaceComponentDecl(
    ChildRef childRef,
    fdecl.Component decl,
  ) {
    return rootRealm.replaceComponentDecl(childRef, decl);
  }

  /// Returns a copy the decl for a child in this realm
  Future<fdecl.Component> getRealmDecl() {
    return rootRealm.getRealmDecl();
  }

  /// Replaces the decl for this realm
  Future<void> replaceRealmDecl(
    fdecl.Component decl,
  ) {
    return rootRealm.replaceRealmDecl(decl);
  }

  /// Load the component's packaged configuration values if available.
  Future<void> initMutableConfigFromPackage(
    ChildRef childRef,
  ) {
    return rootRealm.initMutableConfigFromPackage(childRef);
  }

  /// Allow setting config values without loading packaged values.
  Future<void> initMutableConfigToEmpty(
    ChildRef childRef,
  ) {
    return rootRealm.initMutableConfigToEmpty(childRef);
  }

  /// Replaces a value of a given configuration field
  Future<void> setConfigValue(
    ChildRef childRef,
    String key,
    fconfig.ValueSpec value,
  ) {
    return rootRealm.setConfigValue(childRef, key, value);
  }

  /// Replaces a boolean value of a given configuration field
  Future<void> setConfigValueBool(
    ChildRef childRef,
    String key,
    // ignore: avoid_positional_boolean_parameters
    bool value,
  ) {
    return rootRealm.setConfigValueBool(childRef, key, value);
  }

  /// Replaces a uint8 value of a given configuration field
  Future<void> setConfigValueUint8(
    ChildRef childRef,
    String key,
    int value,
  ) {
    return rootRealm.setConfigValueUint8(childRef, key, value);
  }

  /// Replaces a uint16 value of a given configuration field
  Future<void> setConfigValueUint16(
    ChildRef childRef,
    String key,
    int value,
  ) {
    return rootRealm.setConfigValueUint16(childRef, key, value);
  }

  /// Replaces a uint32 value of a given configuration field
  Future<void> setConfigValueUint32(
    ChildRef childRef,
    String key,
    int value,
  ) {
    return rootRealm.setConfigValueUint32(childRef, key, value);
  }

  /// Replaces a uint64 value of a given configuration field
  Future<void> setConfigValueUint64(
    ChildRef childRef,
    String key,
    int value,
  ) {
    return rootRealm.setConfigValueUint64(childRef, key, value);
  }

  /// Replaces a int8 value of a given configuration field
  Future<void> setConfigValueInt8(
    ChildRef childRef,
    String key,
    int value,
  ) {
    return rootRealm.setConfigValueInt8(childRef, key, value);
  }

  /// Replaces a int16 value of a given configuration field
  Future<void> setConfigValueInt16(
    ChildRef childRef,
    String key,
    int value,
  ) {
    return rootRealm.setConfigValueInt16(childRef, key, value);
  }

  /// Replaces a int32 value of a given configuration field
  Future<void> setConfigValueInt32(
    ChildRef childRef,
    String key,
    int value,
  ) {
    return rootRealm.setConfigValueInt32(childRef, key, value);
  }

  /// Replaces a int64 value of a given configuration field
  Future<void> setConfigValueInt64(
    ChildRef childRef,
    String key,
    int value,
  ) {
    return rootRealm.setConfigValueInt64(childRef, key, value);
  }

  /// Replaces a string value of a given configuration field
  Future<void> setConfigValueString(
    ChildRef childRef,
    String key,
    String value,
  ) {
    return rootRealm.setConfigValueString(childRef, key, value);
  }

  /// Replaces a boolean vector value of a given configuration field
  Future<void> setConfigValueBoolVector(
    ChildRef childRef,
    String key,
    List<bool> value,
  ) {
    return rootRealm.setConfigValueBoolVector(childRef, key, value);
  }

  /// Replaces a uint8 vector value of a given configuration field
  Future<void> setConfigValueUint8Vector(
    ChildRef childRef,
    String key,
    Uint8List value,
  ) {
    return rootRealm.setConfigValueUint8Vector(childRef, key, value);
  }

  /// Replaces a uint16 vector value of a given configuration field
  Future<void> setConfigValueUint16Vector(
    ChildRef childRef,
    String key,
    Uint16List value,
  ) {
    return rootRealm.setConfigValueUint16Vector(childRef, key, value);
  }

  /// Replaces a uint32 vector value of a given configuration field
  Future<void> setConfigValueUint32Vector(
    ChildRef childRef,
    String key,
    Uint32List value,
  ) {
    return rootRealm.setConfigValueUint32Vector(childRef, key, value);
  }

  /// Replaces a uint64 vector value of a given configuration field
  Future<void> setConfigValueUint64Vector(
    ChildRef childRef,
    String key,
    Uint64List value,
  ) {
    return rootRealm.setConfigValueUint64Vector(childRef, key, value);
  }

  /// Replaces a int8 vector value of a given configuration field
  Future<void> setConfigValueInt8Vector(
    ChildRef childRef,
    String key,
    Int8List value,
  ) {
    return rootRealm.setConfigValueInt8Vector(childRef, key, value);
  }

  /// Replaces a int16 vector value of a given configuration field
  Future<void> setConfigValueInt16Vector(
    ChildRef childRef,
    String key,
    Int16List value,
  ) {
    return rootRealm.setConfigValueInt16Vector(childRef, key, value);
  }

  /// Replaces a int32 vector value of a given configuration field
  Future<void> setConfigValueInt32Vector(
    ChildRef childRef,
    String key,
    Int32List value,
  ) {
    return rootRealm.setConfigValueInt32Vector(childRef, key, value);
  }

  /// Replaces a int64 vector value of a given configuration field
  Future<void> setConfigValueInt64Vector(
    ChildRef childRef,
    String key,
    Int64List value,
  ) {
    return rootRealm.setConfigValueInt64Vector(childRef, key, value);
  }

  /// Replaces a string vector value of a given configuration field
  Future<void> setConfigValueStringVector(
    ChildRef childRef,
    String key,
    List<String> value,
  ) {
    return rootRealm.setConfigValueStringVector(childRef, key, value);
  }

  /// Adds a route between components within the realm
  Future<void> addRoute(Route route) {
    return rootRealm.addRoute(route);
  }

  /// Returns the [RealmInstance] so the test can interact with its child
  /// components.
  Future<RealmInstance> build({String? childName}) async {
    final rootUrl =
        await builder.build(rootRealm._localComponentRunnerBuilder.build());
    final root = await ScopedInstance.create(
      childName: childName,
      collectionName: collectionName,
      url: rootUrl,
    );
    root.connectToBinder();
    return RealmInstance(root);
  }
}
