// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of bindings;

// The interface implemented by a class that can implement a function with up
// to 20 unnamed arguments.
abstract class _GenericFunction implements Function {
  const _GenericFunction();

  // Work-around to avoid checked-mode only having grudging support for
  // Function implemented with noSuchMethod. See:
  // https://github.com/dart-lang/sdk/issues/26528
  dynamic call([
      dynamic a1, dynamic a2, dynamic a3, dynamic a4, dynamic a5,
      dynamic a6, dynamic a7, dynamic a8, dynamic a9, dynamic a10,
      dynamic a11, dynamic a12, dynamic a13, dynamic a14, dynamic a15,
      dynamic a16, dynamic a17, dynamic a18, dynamic a19, dynamic a20]);
}

// A class that acts like a function, but which completes a completer with the
// the result of the function rather than returning the result. E.g.:
//
// Completer c = new Completer();
// var completerator = new Completerator._(c, f);
// completerator(a, b);
// await c.future;
//
// This completes the future c with the result of passing a and b to f.
//
// More usefully for Mojo, e.g.:
// await _Completerator.completerate(
//     proxy.method, argList, MethodResponseParams#init);
class _Completerator extends _GenericFunction {
  final Completer _c;
  final Function _toComplete;

  _Completerator._(this._c, this._toComplete);

  static Future completerate(Function f, List args, Function ctor) {
    Completer c = new Completer();
    var newArgs = new List.from(args);
    newArgs.add(new _Completerator._(c, ctor));
    Function.apply(f, newArgs);
    return c.future;
  }

  @override
  dynamic noSuchMethod(Invocation invocation) =>
      (invocation.memberName == #call)
      ? _c.complete(Function.apply(_toComplete, invocation.positionalArguments))
      : super.noSuchMethod(invocation);
}

/// Base class for Proxy class Futurizing wrappers. It turns callback-based
/// methods on the Proxy into Future based methods in derived classes. E.g.:
///
/// class FuturizedHostResolverProxy extends FuturizedProxy<HostResolverProxy> {
///   Map<Symbol, Function> _mojoMethods;
///
///   FuturizedHostResolverProxy(HostResolverProxy proxy) : super(proxy) {
///     _mojoMethods = <Symbol, Function>{
///       #getHostAddresses: proxy.getHostAddresses,
///     };
///   }
///   Map<Symbol, Function> get mojoMethods => _mojoMethods;
///
///   FuturizedHostResolverProxy.unbound() :
///       this(new HostResolverProxy.unbound());
///
///   static final Map<Symbol, Function> _mojoResponses = {
///     #getHostAddresses: new HostResolverGetHostAddressesResponseParams#init,
///   };
///   Map<Symbol, Function> get mojoResponses => _mojoResponses;
/// }
///
/// Then:
///
/// HostResolveProxy proxy = ...
/// var futurizedProxy = new FuturizedHostResolverProxy(proxy);
/// var response = await futurizedProxy.getHostAddresses(host, family);
/// // etc.
///
/// Warning 1: The list of methods and return object constructors in
/// FuturizedHostResolverProxy has to be kept up-do-date by hand with changes
/// to the Mojo interface.
///
/// Warning 2: The recommended API to use is the generated callback-based API.
/// This wrapper class is exposed only for convenience during development,
/// and has no guarantee of optimal performance.
abstract class FuturizedProxy<T extends Proxy> {
  final T _proxy;
  Map<Symbol, Function> get mojoMethods;
  Map<Symbol, Function> get mojoResponses;

  FuturizedProxy(T this._proxy);

  T get proxy => _proxy;
  Future responseOrError(Future f) => _proxy.responseOrError(f);
  Future close({immediate: false}) => _proxy.close(immediate: immediate);

  @override
  dynamic noSuchMethod(Invocation invocation) =>
      mojoMethods.containsKey(invocation.memberName)
          ? _Completerator.completerate(
              mojoMethods[invocation.memberName],
              invocation.positionalArguments,
              mojoResponses[invocation.memberName])
          : super.noSuchMethod(invocation);
}

/// A class that acts like a function that can take up to 20 arguments, and
/// does nothing.
///
/// This class is used in the generated bindings to allow null to be passed for
/// the callback to interface methods implemented by mock services where the
/// result of the method is not needed.
class DoNothingFunction extends _GenericFunction {
  // TODO(zra): DoNothingFunction could rather be implemented just by a function
  // taking some large number of dynamic arguments as we're doing already in
  // _GenericFunction. However, instead of duplicating that hack, we should
  // keep it in once place, and extend from _GenericFunction when we need to
  // use it. Then, if/when there's better support for this sort of thing, we
  // can replace _GenericFunction and propagate any changes needed to things
  // that use it.

  const DoNothingFunction();

  static const DoNothingFunction fn = const DoNothingFunction();

  @override
  dynamic noSuchMethod(Invocation invocation) {
    if (invocation.memberName != #call) {
      return super.noSuchMethod(invocation);
    }
    return null;
  }
}
