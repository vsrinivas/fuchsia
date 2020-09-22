// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';

/// Base class for classes that provide data via [InheritedWidget]s.
abstract class Model implements Listenable {
  final _listeners = <VoidCallback>{};
  final _modifications = <VoidCallback>[];
  int _version = 0;
  int _microtaskVersion = 0;
  bool _notifyingListeners = false;

  /// [listener] will be notified when the model changes.
  @override
  void addListener(VoidCallback listener) {
    if (_notifyingListeners) {
      _modifications.add(() => _listeners.add(listener));
      return;
    }
    _listeners.add(listener);
  }

  /// [listener] will no longer be notified when the model changes.
  @override
  void removeListener(VoidCallback listener) {
    if (_notifyingListeners) {
      _modifications.add(() => _listeners.remove(listener));
      return;
    }
    _listeners.remove(listener);
  }

  /// Returns the number of listeners listening to this model.
  int get listenerCount => _listeners.length;

  /// Should be called only by [Model] when the model has changed.
  void notifyListeners() {
    // We schedule a microtask to debounce multiple changes that can occur
    // all at once.
    if (_microtaskVersion == _version) {
      _microtaskVersion++;
      scheduleMicrotask(() {
        _version++;
        _microtaskVersion = _version;
        _notifyingListeners = true;
        for (VoidCallback listener in _listeners) {
          listener();
        }
        _notifyingListeners = false;

        // If listeners were added or removed during the notification of
        // listeners, make adjustments to the listener set now.
        for (VoidCallback modification in _modifications) {
          modification();
        }
        _modifications.clear();
      });
    }
  }
}

/// Finds a [Model].  This class is necessary as templated classes are relified
/// but static templated functions are not.
class ModelFinder<T extends Model> {
  /// Returns the [Model] of type [T] of the closest ancestor [ScopedModel].
  ///
  /// [Widget]s who call [of] with a [rebuildOnChange] of true will be rebuilt
  /// whenever there's a change to the returned model.
  T of(BuildContext context, {bool rebuildOnChange = false}) {
    // ignore: prefer_const_constructors
    final widget = rebuildOnChange
        ? context.dependOnInheritedWidgetOfExactType<_InheritedModel<T>>()
        : context.findAncestorWidgetOfExactType<_InheritedModel<T>>();
    return (widget is _InheritedModel<T>) ? widget.model : null;
  }
}

/// Allows the given [model] to be accessed by [child] or any of its descendants
/// using [ModelFinder].
class ScopedModel<T extends Model> extends StatelessWidget {
  /// The [Model] to provide to [child] and its descendants.
  final T model;

  /// The [Widget] the [model] will be available to.
  final Widget child;

  /// Constructor.
  const ScopedModel({this.model, this.child});

  @override
  Widget build(BuildContext context) => _ModelListener(
        model: model,
        builder: (BuildContext context) => _InheritedModel<T>(
          model: model,
          child: child,
        ),
      );
}

/// Listens to [model] and calls [builder] whenever [model] changes.
class _ModelListener extends StatefulWidget {
  final Model model;
  final WidgetBuilder builder;

  const _ModelListener({this.model, this.builder});

  @override
  _ModelListenerState createState() => _ModelListenerState();
}

class _ModelListenerState extends State<_ModelListener> {
  @override
  void initState() {
    super.initState();
    widget.model.addListener(_onChange);
  }

  @override
  void didUpdateWidget(_ModelListener oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (widget.model != oldWidget.model) {
      oldWidget.model.removeListener(_onChange);
      widget.model.addListener(_onChange);
    }
  }

  @override
  void dispose() {
    widget.model.removeListener(_onChange);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => widget.builder(context);

  void _onChange() => setState(() {});
}

/// Provides [model] to its [child] [Widget] tree via [InheritedWidget].  When
/// [version] changes, all descendants who request (via
/// [BuildContext.dependOnInheritedWidgetOfExactType]) to be rebuilt when the
/// model changes will do so.
class _InheritedModel<T extends Model> extends InheritedWidget {
  final T model;
  final int version;
  _InheritedModel({Key key, Widget child, this.model})
      : version = model._version,
        super(key: key, child: child);

  /// Used to return the runtime type.
  //ignore: unused_element
  const _InheritedModel.forRuntimeType()
      : model = null,
        version = 0,
        super(child: const Offstage());

  @override
  bool updateShouldNotify(_InheritedModel<T> oldWidget) =>
      oldWidget.version != version;
}

/// Builds a child for a [ScopedModelDescendant].
typedef ScopedModelDescendantBuilder<T extends Model> = Widget Function(
  BuildContext context,
  Widget child,
  T model,
);

/// A [Widget] who rebuilds its child by calling [builder] whenever the [Model]
/// provided by an ancestor [ScopedModel] changes.
class ScopedModelDescendant<T extends Model> extends StatelessWidget {
  /// Called whenever the [Model] changes.
  final ScopedModelDescendantBuilder<T> builder;

  /// An optional constant child that does not depend on the model.  This will
  /// be passed as the child of [builder].
  final Widget child;

  /// Constructor.
  const ScopedModelDescendant({this.builder, this.child});

  @override
  Widget build(BuildContext context) => builder(
        context,
        child,
        ModelFinder<T>().of(context, rebuildOnChange: true),
      );
}

/// Mixin to enable a model to provide tickers for animations.
mixin TickerProviderModelMixin on Model implements TickerProvider {
  final _tickers = <Ticker>{};

  /// Creates a ticker with the given callback.
  @override
  Ticker createTicker(TickerCallback onTick) {
    final ticker = Ticker(onTick);
    _tickers.add(ticker);
    return ticker;
  }

  /// Closes out any active tickers.
  void dispose() {
    for (Ticker ticker in _tickers) {
      ticker.dispose();
    }
  }
}
