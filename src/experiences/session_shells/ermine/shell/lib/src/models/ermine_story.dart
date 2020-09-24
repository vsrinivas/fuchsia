// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_session/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_scenic_flutter/child_view_connection.dart';
import 'package:fuchsia_services/services.dart';
import 'package:uuid/uuid.dart';
import 'package:zircon/zircon.dart';

import '../utils/presenter.dart';
import '../utils/suggestion.dart';

const double kFrameTime60FpsInMilliseconds = 16.3333;

/// A function which can be used to launch the suggestion.
typedef LaunchSuggestion = Future<void> Function(
    Suggestion, ElementControllerProxy);

/// Defines a class to represent a story in ermine.
class ErmineStory {
  final ValueChanged<ErmineStory> onDelete;
  final ValueChanged<ErmineStory> onChange;
  final String id;

  // An optional view controller which allows the story to communicate with the
  // process.
  ViewControllerImpl viewController;

  ViewRef viewRef;

  // An optional element controller which allows the story to communicate with
  // the element. This will only be available if ermine launched this process.
  ElementControllerProxy _elementController;

  /// Creates a launches an ermine story.
  @visibleForTesting
  ErmineStory({
    this.id,
    this.onDelete,
    this.onChange,
    String title = '',
  })  : nameNotifier = ValueNotifier(title),
        childViewConnectionNotifier = ValueNotifier(null);

  factory ErmineStory.fromSuggestion({
    Suggestion suggestion,
    ValueChanged<ErmineStory> onDelete,
    ValueChanged<ErmineStory> onChange,
    LaunchSuggestion launchSuggestion,
  }) {
    final elementController = ElementControllerProxy();
    launchSuggestion ??= ErmineStory.launchSuggestion;
    launchSuggestion(suggestion, elementController);
    return ErmineStory(
      id: suggestion.id,
      title: suggestion.title,
      onDelete: onDelete,
      onChange: onChange,
    ).._elementController = elementController;
  }

  /// Creates an ermine story which was proposed from an external source.
  ///
  /// This method will not attempt to launch a story but will generate
  /// a random suggestion
  factory ErmineStory.fromExternalSource({
    ValueChanged<ErmineStory> onDelete,
    ValueChanged<ErmineStory> onChange,
  }) {
    final id = Uuid().v4();
    return ErmineStory(
      id: 'external:$id',
      onDelete: onDelete,
      onChange: onChange,
    );
  }

  final ValueNotifier<String> nameNotifier;
  String get name => nameNotifier.value ?? id;
  set name(String value) => nameNotifier.value = value;

  ValueNotifier<bool> focusedNotifier = ValueNotifier(false);
  bool get focused => focusedNotifier.value;
  set focused(bool value) => focusedNotifier.value = value;

  final ValueNotifier<ChildViewConnection> childViewConnectionNotifier;
  ChildViewConnection get childViewConnection =>
      childViewConnectionNotifier.value;

  ValueNotifier<bool> fullscreenNotifier = ValueNotifier(false);
  bool get fullscreen => fullscreenNotifier.value;
  set fullscreen(bool value) => fullscreenNotifier.value = value;
  bool get isImmersive => fullscreenNotifier.value == true;

  void delete() {
    childViewConnectionNotifier.value?.dispose();
    childViewConnectionNotifier.value = null;
    viewController?.close();
    onDelete?.call(this);
    _elementController?.ctrl?.close();
  }

  /// Sets the focus state on this story.
  ///
  /// Also invokes [onChange] callback and request scenic to transfer input
  /// focus to the associated [viewRef].
  ///
  /// Takes an optional [ViewRefInstalledProxy] to allow passing in a mocked
  /// instance during test.
  void focus([ViewRefInstalledProxy viewRefInstalled]) {
    onChange?.call(this..focused = true);
    requestFocus(viewRefInstalled);
  }

  void maximize() => onChange?.call(this..fullscreen = true);

  void restore() => onChange?.call(this..fullscreen = false);

  static Future<void> launchSuggestion(
      Suggestion suggestion, ElementControllerProxy elementController) async {
    final proxy = ElementManagerProxy();

    StartupContext.fromStartupInfo().incoming.connectToService(proxy);

    final annotations = Annotations(customAnnotations: [
      Annotation(
        key: ermineSuggestionIdKey,
        value: Value.withText(suggestion.id),
      ),
    ]);

    final spec =
        ElementSpec(componentUrl: suggestion.url, annotations: annotations);

    await proxy
        .proposeElement(spec, elementController.ctrl.request())
        .catchError((err) {
      log.shout('$err: Failed to propose elememnt <${suggestion.url}>');
    });

    proxy.ctrl.close();
  }

  void presentView(
      ChildViewConnection connection, ViewRef viewRef, ViewControllerImpl vc) {
    childViewConnectionNotifier.value = connection;
    this.viewRef = viewRef;
    viewController = vc;
    viewController?.didPresent();
  }

  /// Requests focus to be transfered to this view given it's [viewRef].
  Future<void> requestFocus([ViewRefInstalledProxy viewRefInstalled]) async {
    // [requestFocus] is called for 'every' post render of ChildView widget,
    // even when that child view is not focused. Skip focusing those views here.
    if (childViewConnection == null || !focused) {
      return;
    }

    // TODO(60528): Wait until child view is connected to view tree. Until the
    // view connected event is plumbed to [ChildViewConnection], we use a flaky
    // delay of 3 frames before attempting to request focus for the child view.
    await Future.delayed(Duration(
      milliseconds: (kFrameTime60FpsInMilliseconds * 3).toInt(),
    ));

    final viewRefService = viewRefInstalled ?? ViewRefInstalledProxy();
    if (viewRefInstalled == null) {
      StartupContext.fromStartupInfo()
          .incoming
          .connectToService(viewRefService);
    }
    try {
      // Wait for [viewRef] to be attached to the view tree.
      final eventPair = viewRef.reference.duplicate(ZX.RIGHT_SAME_RIGHTS);
      assert(eventPair.isValid);

      await viewRefService.watch(ViewRef(reference: eventPair));
      childViewConnection.requestFocus();
    } on Exception catch (_) {}
  }
}
