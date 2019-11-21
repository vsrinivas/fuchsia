// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fuchsia_modular/fidl_async.dart' as modular;
import 'package:flutter/material.dart';
import 'package:flutter/services.dart'
    show RawKeyDownEvent, RawKeyEventDataFuchsia;
import 'package:internationalization/strings.dart';

import '../utils/styles.dart';
import '../utils/suggestions.dart';

/// Defines a model that holds the state for the [Ask] widget.
///
/// Holds the list of suggestions and auto-complete entries retrieved from the
/// suggestion engine.
class AskModel extends ChangeNotifier {
  /// The [TextEditingController] used by Ask [TextField] widget.
  final TextEditingController controller = TextEditingController();

  /// The [FocusNode] use to control focus on the Ask [TextField].
  final FocusNode focusNode = FocusNode();

  /// The [GlobalKey] associated with the animated suggestions list.
  final GlobalKey<AnimatedListState> suggestionsListKey =
      GlobalKey(debugLabel: 'suggestionsList');

  /// Callback when Ask is dismissed by the user.
  final VoidCallback onDismiss;

  final ValueNotifier<List<Suggestion>> suggestions =
      ValueNotifier(<Suggestion>[]);
  final ValueNotifier<int> selection = ValueNotifier(-1);

  // Keyboard HID usage values defined in:
  // https://www.usb.org/sites/default/files/documents/hut1_12v2.pdf
  static const int kUpArrow = 82;
  static const int kDownArrow = 81;
  static const int kPageDown = 78;
  static const int kPageUp = 75;

  final SuggestionService _suggestionService;
  final modular.PuppetMaster _puppetMaster;
  String _currentQuery;
  StreamSubscription<Iterable<Suggestion>> _suggestionStream;

  // List of built-in suggestions show when ask box is empty.
  static final List<Suggestion> builtInSuggestions = <Suggestion>[
    Suggestion(
      id: 'fuchsia-pkg://fuchsia.com/simple_browser#meta/simple_browser.cmx',
      displayInfo: DisplayInfo(
        title: Strings.openPackage('simple_browser'),
        subtitle: 'simple_browser',
      ),
    ),
    Suggestion(
      id: 'fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cmx',
      displayInfo: DisplayInfo(
        title: Strings.openPackage('terminal'),
        subtitle: 'terminal',
      ),
    ),
    Suggestion(
      id: 'fuchsia-pkg://fuchsia.com/settings#meta/settings.cmx',
      displayInfo: DisplayInfo(
        title: Strings.openPackage('settings'),
        subtitle: 'settings',
      ),
    ),
  ];

  /// Constructor.
  AskModel({
    @required SuggestionService suggestionService,
    @required modular.PuppetMaster puppetMaster,
    this.onDismiss,
  })  : _suggestionService = suggestionService,
        _puppetMaster = puppetMaster;

  @override
  void dispose() {
    super.dispose();
    controller.dispose();
  }

  /// Called by [TextField]'s [onChanged] callback when text changes.
  ///
  /// Send the text to the Suggestion service to get suggestions.
  void query(String text) {
    if (text == _currentQuery || !controller.selection.isCollapsed) {
      return;
    }
    _currentQuery = text;

    // Cancel existing suggestion stream.
    _suggestionStream?.cancel();

    // Get suggestions from Suggestions service.
    _suggestionStream =
        _suggestionService.getSuggestions(text).asStream().listen((result) {
      // Remove existing entries from [AnimatedListState].
      for (int index = 0; index < suggestions.value.length; index++) {
        _removeItem(index);
      }

      suggestions.value = result.isNotEmpty || _currentQuery.isNotEmpty
          ? result.toList()
          : builtInSuggestions;

      // Insert the suggestion in [AnimatedListState].
      for (int index = 0; index < suggestions.value.length; index++) {
        _insertItem(index);
      }

      if (result.isNotEmpty) {
        selection.value = 0;
      }

      _suggestionStream.cancel();
    });
  }

  /// Called by [TextField]'s [onSubmitted] callback.
  ///
  /// Invokes the selected suggestion.
  void submit(String query) {
    // If there are no suggestions, set focus back on text field.
    if (suggestions.value.isEmpty) {
      focusNode.requestFocus();
      return;
    }
    // Use the top suggestion (highest confidence) if user did not select
    // another suggestion from the list.
    final suggestion = (selection.value < 0)
        ? suggestions.value.first
        : suggestions.value[selection.value];
    handleSuggestion(suggestion);

    // Dismiss Ask.
    onDismiss?.call();
  }

  /// Called by [RawKeyboardListener]'s [onKey] callback.
  ///
  /// Updates the selection on the suggestions list based on key pressed.
  void handleKey(RawKeyEvent event) {
    RawKeyEventDataFuchsia data = event.data;
    // We only process pure key down events: codePoint = 0 and modifiers = 0.
    int newSelection = selection.value;
    if (event is RawKeyDownEvent &&
        data.codePoint == 0 &&
        data.modifiers == 0) {
      switch (data.hidUsage) {
        case kDownArrow:
          newSelection++;
          break;
        case kUpArrow:
          newSelection--;
          break;
        case kPageDown:
          newSelection += 5;
          break;
        case kPageUp:
          newSelection -= 5;
          break;
        default:
          return;
      }
      if (suggestions.value.isNotEmpty) {
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
  }

  /// Called when a suggestion was tapped/clicked by the user.
  void handleSuggestion(Suggestion suggestion) {
    if (suggestion.id.startsWith('fuchsia-pkg')) {
      _launchBuiltInSuggestion(suggestion);
    } else {
      _suggestionService.invokeSuggestion(suggestion);
    }
  }

  void _launchBuiltInSuggestion(Suggestion suggestion) {
    final storyMaster = modular.StoryPuppetMasterProxy();
    _puppetMaster.controlStory(
      suggestion.displayInfo.subtitle,
      storyMaster.ctrl.request(),
    );
    final addMod = modular.AddMod(
      intent: modular.Intent(action: '', handler: suggestion.id),
      surfaceParentModName: [],
      modName: ['root'],
      surfaceRelation: modular.SurfaceRelation(),
    );
    storyMaster
      ..enqueue([
        modular.StoryCommand.withAddMod(addMod),
        modular.StoryCommand.withSetFocusState(
            modular.SetFocusState(focused: true)),
      ])
      ..execute();
  }

  void _insertItem(int index) {
    if (suggestionsListKey.currentState != null) {
      Duration duration = ErmineStyle.kAskItemAnimationDuration;
      suggestionsListKey.currentState.insertItem(index, duration: duration);
    }
  }

  void _removeItem(int index) {
    if (suggestionsListKey.currentState != null) {
      suggestionsListKey.currentState.removeItem(
        0,
        (context, animation) => Offstage(),
        duration: Duration.zero,
      );
    }
  }
}
