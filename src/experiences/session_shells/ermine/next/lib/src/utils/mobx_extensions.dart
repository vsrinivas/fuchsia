// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:mobx/mobx.dart';

/// Turn Object into an Observable.
extension MobxObjectExtension<T> on T {
  Observable<T> asObservable({ReactiveContext? context, String? name}) =>
      Observable<T>(this, context: context, name: name);

  ObservableFuture asObservableFuture(
          {ReactiveContext? context, String? name}) =>
      ObservableFuture<T>.value(this, context: context, name: name);
}

/// Turn Function into [Computed].
extension MobxComputedFunctionExtension<T> on T Function() {
  Computed<T> asComputed({ReactiveContext? context, String? name}) =>
      Computed<T>(this, context: context, name: name);
}

/// Turns a generic Function into [Action].
extension MobxActionFunctionExtension on Function {
  Action asAction({ReactiveContext? context, String? name}) =>
      Action(this, context: context, name: name);
}
