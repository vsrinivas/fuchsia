// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart'
    show RawKeyDownEvent, RawKeyEventDataFuchsia;

import 'package:fidl/fidl.dart' show InterfaceRequest;
import 'package:fidl_fuchsia_app_discover/fidl_async.dart';
import 'package:fidl_fuchsia_shell_ermine/fidl_async.dart'
    show AskBar, AskBarBinding;
import 'package:fuchsia_services/services.dart' show StartupContext;

const int _kMaxSuggestions = 20;

// Keyboard HID usage values defined in:
// https://www.usb.org/sites/default/files/documents/hut1_12v2.pdf
const int _kUpArrow = 82;
const int _kDownArrow = 81;
const int _kPageDown = 78;
const int _kPageUp = 75;
const int _kEsc = 41;

class AskModel extends ChangeNotifier {
  final StartupContext startupContext;
  final TextEditingController controller = TextEditingController();
  final FocusNode focusNode = FocusNode();
  final ValueNotifier<bool> visibility = ValueNotifier(false);
  final ValueNotifier<List<Suggestion>> suggestions =
      ValueNotifier(<Suggestion>[]);
  final ValueNotifier<int> selection = ValueNotifier(-1);

  double autoCompleteTop = 0;
  double elevation = 200.0;

  _AskImpl _ask;
  String _currentQuery;

  final _askBinding = AskBarBinding();
  final _suggestionService = SuggestionsProxy();

  // Holds the suggestion results until query completes.
  List<Suggestion> _suggestions = <Suggestion>[];

  AskModel({this.startupContext}) {
    _ask = _AskImpl(this);
    StartupContext.fromStartupInfo()
        .incoming
        .connectToService(_suggestionService);
  }

  void focus(BuildContext context) =>
      FocusScope.of(context).requestFocus(focusNode);

  void unfocus() => focusNode.unfocus();

  bool get isVisible => visibility.value;

  ValueNotifier<ImageProvider> imageFromSuggestion(Suggestion suggestion) {
    final image = ValueNotifier<ImageProvider>(null);
    if (suggestion?.displayInfo?.icon != null) {
      image.value = NetworkImage(suggestion.displayInfo.icon);
    }
    return image;
  }

  // ignore: use_setters_to_change_properties
  void load(double elevation) {
    this.elevation = elevation;
  }

  void show() {
    visibility.value = true;
    _ask.fireVisible();
    controller.clear();
    onQuery('');
  }

  void hide() {
    visibility.value = false;
  }

  /// Called from ui when hiding animation has completed.
  void hideAnimationCompleted() {
    _suggestions = <Suggestion>[];
    suggestions.value = _suggestions;
    selection.value = -1;
    controller.clear();
    _ask.fireHidden();
  }

  void onAsk(String query) {
    // If there are no suggestions, do nothing.
    if (suggestions.value.isEmpty) {
      return;
    }
    // Use the top suggestion (highest confidence) if user did not select
    // another suggestion from the list.
    if (selection.value < 0) {
      onSelect(suggestions.value.first);
    } else {
      onSelect(suggestions.value[selection.value]);
    }
  }

  void onKey(RawKeyEvent event) {
    RawKeyEventDataFuchsia data = event.data;
    // We only process pure key down events: codePoint = 0 and modifiers = 0.
    int newSelection = selection.value;
    if (event is RawKeyDownEvent &&
        suggestions.value.isNotEmpty &&
        data.codePoint == 0 &&
        data.modifiers == 0) {
      switch (data.hidUsage) {
        case _kEsc:
          hide();
          return;
        case _kDownArrow:
          newSelection++;
          break;
        case _kUpArrow:
          newSelection--;
          break;
        case _kPageDown:
          newSelection += 5;
          break;
        case _kPageUp:
          newSelection -= 5;
          break;
        default:
          return;
      }
      selection.value = newSelection.clamp(0, suggestions.value.length - 1);
      controller.value = controller.value.copyWith(
        text: suggestions.value[selection.value].displayInfo.title,
        selection: TextSelection(
          baseOffset: 0,
          extentOffset:
              suggestions.value[selection.value].displayInfo.title.length,
        ),
      );
    }
  }

  void onQuery(String query) async {
    if (query == _currentQuery || !controller.selection.isCollapsed) {
      return;
    }
    _currentQuery = query;
    final newSuggestions = <Suggestion>[];

    final iterator = SuggestionsIteratorProxy();
    await _suggestionService.getSuggestions(query, iterator.ctrl.request());

    Iterable<Suggestion> partialResults;
    while ((partialResults = await iterator.next()).isNotEmpty &&
        newSuggestions.length < _kMaxSuggestions) {
      newSuggestions.addAll(partialResults);
    }
    final result = List<Suggestion>.from(newSuggestions.take(_kMaxSuggestions));
    _suggestions = result;
    suggestions.value = result;
  }

  void onSelect(Suggestion suggestion) {
    _suggestionService.notifyInteraction(
        suggestion.id, InteractionType.selected);
    hide();
  }

  void advertise() {
    startupContext.outgoing.addPublicService(
      (InterfaceRequest<AskBar> request) => _askBinding.bind(_ask, request),
      AskBar.$serviceName,
    );
  }
}

class _AskImpl extends AskBar {
  final AskModel askModel;
  final _onHiddenStream = StreamController<void>();
  final _onVisibleStream = StreamController<void>();

  _AskImpl(this.askModel);

  void close() {
    _onHiddenStream.close();
    _onVisibleStream.close();
  }

  void fireHidden() => _onHiddenStream.add(null);

  void fireVisible() => _onVisibleStream.add(null);

  @override
  Future<void> show() async => askModel.show();

  @override
  Future<void> hide() async => askModel.hide();

  @override
  Future<void> load(double elevation) async => askModel.load(elevation);

  @override
  Stream<void> get onHidden => _onHiddenStream.stream;

  @override
  Stream<void> get onVisible => _onVisibleStream.stream;
}
