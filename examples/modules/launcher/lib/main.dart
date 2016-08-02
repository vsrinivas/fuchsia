// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:math';
import 'dart:ui' as ui;

import 'package:flutter/services.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:modular_flutter/flutter_module.dart';
import 'package:modular/log.dart' as logging;
import 'package:modular/modular/handler.mojom.dart';
import 'package:modular/modular/compose.mojom.dart';
import 'package:modular_core/uuid.dart' show Uuid;
import 'package:modular_services/suggestinator/suggestions.mojom.dart';
import 'package:mojo_services/activity/activity.mojom.dart' as mojom;
import 'package:representation_types/person.dart';

import 'notify.dart';
import 'people_bar.dart';
import 'person_component.dart';
import 'story.dart';
import 'story_view.dart';
import 'suggestions_adapter.dart';
import 'suggestions_list.dart';
import 'ui/speech_input.dart';
import 'ui/enter_exit_transition.dart';
import 'users.dart';
import 'wallpaper.dart';

const bool _kIncludeContextInStoryView = false;

const String kUserLabel = 'user';
const String kUsernameLabel = 'username';
const String kStoryLabel = 'story';
const String kSharedStoryLabel = 'shared-story';
const TextStyle _kContextTextStyle = const TextStyle(
    fontSize: 10.0,
    fontWeight: FontWeight.w400,
    fontFamily: 'Roboto',
    letterSpacing: 1.5);
const TextStyle _kInlineVoiceTextStyle = const TextStyle(
    fontSize: 10.0,
    fontWeight: FontWeight.w900,
    fontFamily: 'Roboto',
    letterSpacing: 1.5);
const TextStyle _kSpeechTextStyle = const TextStyle(
    fontSize: 24.0,
    fontWeight: FontWeight.w200,
    fontFamily: 'Roboto',
    height: 1.4);
const double _kDeviceExtensionHeight = 40.0;
const double _kModuleAreaHeightAfterSpeech = 376.0;

class LauncherState extends SimpleModularState {
  static const String _activityString = 'Listening to Metronomy at home';

  final logging.Logger _log = logging.log('LauncherState');

  HandlerServiceProxy handlerService;

  final GlobalKey<SpeechInputState> _speechInputKey =
      new GlobalKey<SpeechInputState>();
  final GlobalKey<WallpaperState> _wallpaperKey =
      new GlobalKey<WallpaperState>();
  final GlobalKey<SuggestionListState> _suggestionListKey =
      new GlobalKey<SuggestionListState>();
  final GlobalKey<ScrollableState> _scrollableKey =
      new GlobalKey<ScrollableState>();
  final GlobalKey<StoryViewState> _storyViewKey =
      new GlobalKey<StoryViewState>();
  final GlobalKey<SimulatedHeightWidgetState> _storyViewHeightKey =
      new GlobalKey<SimulatedHeightWidgetState>();
  final GlobalKey<SimulatedHeightWidgetState> _nonStoryViewHeightKey =
      new GlobalKey<SimulatedHeightWidgetState>();
  final GlobalKey<ActivatableWidgetState> _voiceInputTextTransitionKey =
      new GlobalKey<ActivatableWidgetState>();
  final GlobalKey<EnterExitTransitionState> _voiceInputInlineTextTransitionKey =
      new GlobalKey<EnterExitTransitionState>();
  final GlobalKey<SpeechTextState> _speechTextKey =
      new GlobalKey<SpeechTextState>();
  final GlobalKey<SpeechTextState> _speechContextTextKey =
      new GlobalKey<SpeechTextState>();
  final GlobalKey<PeopleBarContainerState> _peopleBarKey =
      new GlobalKey<PeopleBarContainerState>();

  Users _users;

  SuggestionsAdapter suggestionsAdapter;

  String _latestSelectedSuggestionDescription = '';
  String _currentSpeechText = '';
  String _previousSpeechText = '';

  // Launcher manages all the sessions created/restored by the user.
  // This is the current running session. When the launcher is started, there
  // will not be any session started/restored by the user. The current running
  // session gets set when user starts selecting either 'Create new session' or
  // 'Restore old session' suggestions.
  Story _activeStory;
  bool _shouldShowPeopleBar = false;

  List<Story> _allStories = <Story>[];
  final Notifier notifier = new Notifier();

  List<Widget> _composeChildren = <Widget>[];
  ComposableProxy _currentFullComposer;

  final Map<Uuid, LiveSuggestionData> _liveSuggestions =
      <Uuid, LiveSuggestionData>{};

  double _moduleAreaHeight = 0.0;
  SpeechInput _speechInput;

  // While true, only the lockscreen/wallpaper will be shown.
  bool _lockscreenOverride = true;

  Completer<Null> _firstBuildCompleter = new Completer<Null>();

  String _getWeekdayString(final DateTime date) {
    int weekday = date.weekday;
    assert(weekday > 0 && weekday < 8);
    switch (weekday) {
      case DateTime.MONDAY:
        return 'Monday';
      case DateTime.TUESDAY:
        return 'Tuesday';
      case DateTime.WEDNESDAY:
        return 'Wednesday';
      case DateTime.THURSDAY:
        return 'Thursday';
      case DateTime.FRIDAY:
        return 'Friday';
      case DateTime.SATURDAY:
        return 'Saturday';
      case DateTime.SUNDAY:
        return 'Sunday';
    }
    return 'unknown';
  }

  String get _currentTimeAndDay {
    final DateTime now = new DateTime.now();
    return '${now.hour}:${now.minute < 10 ? 0 : ''}${now.minute} '
        'on ${_getWeekdayString(now)}';
  }

  String get _contextString {
    if (_previousSpeechText.isNotEmpty) {
      return '"$_previousSpeechText"';
    }
    if (_latestSelectedSuggestionDescription.isNotEmpty) {
      return _latestSelectedSuggestionDescription;
    }

    // TODO(armansito): If the current context text that is being displayed is
    // the time and day, then the UI needs to be updated every minute for this
    // to behave like a real clock.
    return _currentTimeAndDay;
  }

  double get maxModuleAreaHeight {
    return ui.window.size.height - _kDeviceExtensionHeight;
  }

  @override
  void initState() {
    super.initState();

    mojom.ActivityProxy activity = new mojom.ActivityProxy.unbound();
    try {
      activity = shell.connectToApplicationService(
          "mojo:android", mojom.Activity.connectToService);
      activity.setSystemUiVisibility(mojom.SystemUiVisibility.immersive);
    } catch (exception) {
      print("Failed to set immersive: $exception");
    } finally {
      activity.close();
    }
  }

  @override
  void dispose() {
    suggestionsAdapter.removeListener(onSuggestionsUpdated);
    super.dispose();
  }

  void initHandlerConnection() {
    handlerService = shell.connectToApplicationService(
        'https://tq.mojoapps.io/handler.mojo', HandlerService.connectToService);

    _users = new Users(_onAvatarImageUrlUpdate);
    _speechInput = new SpeechInput(
        key: _speechInputKey,
        onSpeechRecognized: _onSpeechRecognized,
        onListeningChanged: (bool isListening) => setState(() {
              if (isListening) {
                _lockscreenOverride = false;
                _previousSpeechText = _currentSpeechText;
                _speechText = '';
                _scrollableKey.currentState?.scrollTo(0.0,
                    duration: const Duration(milliseconds: 350),
                    curve: Curves.fastOutSlowIn);
              } else {
                setState(() {
                  _moduleAreaHeight = _kModuleAreaHeightAfterSpeech;
                });
              }
              _adjustVoiceInputText();
              _adjustWallpaper();
              _adjustStoryView();
            }));
    linkUserManagerSession();
  }

  void initSuggestinatorConnection() {
    SuggestionServiceProxy suggestionProxy = shell.connectToApplicationService(
        'https://tq.mojoapps.io/suggestinator.mojo',
        SuggestionService.connectToService);
    suggestionsAdapter = new SuggestionsAdapter(suggestionProxy);
    suggestionsAdapter.addListener(onSuggestionsUpdated);
  }

  @override // ModularState
  void onChange() {
    if (suggestionsAdapter == null) {
      initHandlerConnection();
      initSuggestinatorConnection();
    }
    Iterable<String> allStoryIds =
        _allStories.map((final Story story) => story.sessionId);
    final List<Story> newStories = sessionRoot
        .getList(<String>[kStoryLabel]).where((final SemanticNode storyNode) {
      final String sessionId = storyNode.get(<String>[kSessionIdLabel]).value;
      return (sessionId != null && !allStoryIds.contains(sessionId));
    }).map((final SemanticNode storyNode) {
      return new Story(storyNode.get(<String>[kSessionIdLabel]).value,
          storyNode: storyNode);
    }).toList();
    _allStories.addAll(newStories);
    _notifySharedStoriesIfExist();
  }

  @override // ModularState
  Future<Null> onCompose(Iterable<ModuleComposeData> childModulesData) async {
    // Wait for first build to happen so that state objects for all keys used in
    // the class are created.
    await _firstBuildCompleter.future;

    _log.info('onCompose');

    setState(() {
      Widget latestFullWidget;
      _composeChildren.clear();
      _liveSuggestions.clear();
      for (final ModuleComposeData childData in childModulesData) {
        final Map<String, Widget> embodimentMap = childData.embodimentMap;
        // The proposal here is that, 'card' widgets can share the screen and
        // 'full' widgets doesn't.
        // TODO(ksimbili): In this mix of 'card' and 'full' widgets, we consider
        // only the latest set in the order they get pushed onto the composition
        // tree. We need to figure out the right ordering of these.
        latestFullWidget = embodimentMap["full"] ?? latestFullWidget;
        if (childData.moduleData.liveSuggestionId != null) {
          assert(embodimentMap.containsKey('suggestion'));
          final Uuid suggestionId =
              Uuid.fromBase64(childData.moduleData.liveSuggestionId);
          _liveSuggestions[suggestionId] = new LiveSuggestionData(
              suggestionId, embodimentMap['suggestion'], childData);
        } else if (latestFullWidget != null) {
          // On every 'full' widget, we clear the child widgets to show only
          // the latest set of widgets.
          _composeChildren.clear();
          childData.composableProxy.display("full");
          _currentFullComposer = childData.composableProxy;
        } else {
          assert(embodimentMap.containsKey("card") ||
              embodimentMap.containsKey("empty"));
          _composeChildren.add(embodimentMap["card"] ?? embodimentMap["empty"]);
          childData.composableProxy.display("card");
        }
      }
      for (final LiveSuggestionData data in _liveSuggestions.values) {
        data.composeData.composableProxy.display('suggestion');
      }
      if (latestFullWidget != null && _composeChildren.isEmpty) {
        _composeChildren.add(latestFullWidget);
      }
      if (_composeChildren.isNotEmpty) {
        if (_lockscreenOverride) {
          _moduleAreaHeight = maxModuleAreaHeight;
        }
        _lockscreenOverride = false;
      }
      _adjustVoiceInputText();
      _adjustWallpaper();
      _adjustStoryView();
    });
  }

  @override // FlutterModule
  Future<bool> onBack() async {
    // If we're displaying a full screen module, give it a chance to handle the
    // back button.
    if (_currentFullComposer != null) {
      Completer<bool> completer = new Completer<bool>();
      _currentFullComposer.back((bool wasHandled) {
        if (wasHandled) {
          completer.complete(true);
        } else {
          completer.complete(super.onBack());
        }
      });
      return completer.future;
    }
    return super.onBack();
  }

  void linkUserManagerSession() {
    // User manager session id is defined in user_maanger.dart:sessionId which
    // is set to all '0's. This is the base64 version of it.
    final String userManagerSessionId = 'AAAAAAAAAAAAAAAAAAAAAA==';

    handlerService.linkSession(userManagerSessionId,
        (final HandlerStatus status) {
      if (status != HandlerStatus.ok) {
        _log.severe('Failed to link user data graph');
      }
    });
  }

  // We compute session root ourselves. The reason is, the way root is computed
  // in state_graph.dart is by calling the expression anchors.first. But it
  // happens that anchors.first is always not the same node.
  // So for this module, a root is a node which doesn't contains any user edges.
  // The node with user edges is coming from user_manager graph.
  SemanticNode get sessionRoot => state.anchors.firstWhere(
      (final SemanticNode anchor) =>
          anchor.getList(<String>[kUserLabel]).isEmpty,
      orElse: null);

  /// Adds a new speech utterance to the session.
  void _addSpeechInput(String input) {
    updateSession((SemanticNode session) async {
      if (_activeStory == null) {
        await _createStory();
      }
      _activeStory.addSpeechInput(input);
    });
  }

  // Reads shared sessions from given user node. Right now, we only store
  // session Ids. But we can store informations like whether it is read or not
  // etc.
  List<String> _readSharedSessionIdsFromUser(String username) {
    final SemanticNode user = _users.getUser(state, username);
    if (user == null) {
      return <String>[];
    }
    return user
        .getList(<String>[kSharedStoryLabel])
        .map((final SemanticNode sharedStory) => sharedStory.value)
        .toList();
  }

  // Writes the session id to the user node. Sharing of a session with another
  // user will just write the session id to other users node.
  void _writeSharedSessionIdsToUser(String username, String sessionId) {
    final SemanticNode user = _users.getUser(state, username);
    if (user != null) {
      _log.info('Sharing with user: $username');
      updateSession((SemanticNode session) {
        user.create(<String>[kSharedStoryLabel]).value = sessionId;
      });
    }
  }

  // Shows notifications if there is any new shared story.
  // Note, currently every device(with _currentUsername) will show the
  // notification.
  void _notifySharedStoriesIfExist() {
    final Set<String> sharedStoryIds = new Set<String>.from(
        _readSharedSessionIdsFromUser(_users.getCurrentUsername(state)));
    if (sharedStoryIds.isEmpty) {
      return;
    }
    Iterable<String> allStoryIds =
        _allStories.map((final Story story) => story.sessionId);
    sharedStoryIds
        .where((String id) => !allStoryIds.contains(id))
        .map((String id) => new Story(id, title: 'Shared story'))
        .forEach((final Story sharedStory) {
      _allStories.add(sharedStory);
      // TODO(ksimbili): Merge multiple notfications to one.
      notifier.postNotification(sharedStory, _openStory);
    });
  }

  // Sets the active story. If the story is started with empty story, then
  // voice input recognition will be started too.
  void _setActiveStory(Story story, {bool emptySession: false}) {
    ModalRoute.of(_wallpaperKey.currentContext).addLocalHistoryEntry(
        new LocalHistoryEntry(onRemove: _closeActiveStory));

    // TODO(armansito): Suggestinator needs to be somehow notified so that it
    // can properly assign relevance to the correct suggestions immediately when
    // the story changes.
    setState(() {
      _activeStory = story;
      _shouldShowPeopleBar = !emptySession;
      if (emptySession) {
        // TODO(apwilson): Trigger voice input?
      }

      // If we just opened a shared story (story with no node), then we create
      // and assign a node to it. This is how we know not to notify the user
      // for a shared story that they have already opened.
      if (story.node != null) return;
      updateSession((SemanticNode session) {
        story.node = sessionRoot.create(<String>[kStoryLabel]);
      });
    });
  }

  void _closeActiveStory() {
    if (_activeStory == null) {
      return;
    }

    handlerService.stopSession(_activeStory.sessionId,
        (final HandlerStatus status) {
      if (status != HandlerStatus.ok) {
        _log.severe('Failed to stop session (status: $status)');
      }
    });
    setState(() {
      _activeStory = null;
      _latestSelectedSuggestionDescription = '';
    });
  }

  void onSuggestionsUpdated() {
    _adjustWallpaper();
  }

  void _onAvatarImageUrlUpdate() {
    _peopleBarKey.currentState?.setState(() {});
  }

  void _onSelectSuggestion(String sessionId, String suggestionDescription) {
    if (sessionId == null) return;
    _latestSelectedSuggestionDescription = suggestionDescription;
    _previousSpeechText = '';
    _speechText = '';
    setState(() {
      _moduleAreaHeight = maxModuleAreaHeight;
      _adjustStoryView();
    });
    _scrollableKey.currentState?.scrollTo(0.0,
        duration: const Duration(milliseconds: 350),
        curve: Curves.fastOutSlowIn);
    if (_allStories.any((final Story story) => story.sessionId == sessionId)) {
      _shouldShowPeopleBar = true;
      return;
    }
    updateSession((SemanticNode session) {
      final Story newStory = new Story(sessionId,
          storyNode: sessionRoot.create(<String>[kStoryLabel]),
          title: suggestionDescription);

      _allStories.add(newStory);

      // TODO(armansito): We hard-code false here, because suggestions that
      // create a new session are never empty.
      _setActiveStory(newStory, emptySession: false);
    });
  }

  Future<Null> _createStory({String jsonRecipe}) {
    _closeActiveStory();
    Completer<Null> completer = new Completer<Null>();
    handlerService.createSession(jsonRecipe,
        (final HandlerStatus status, final String sessionId) {
      if (status != HandlerStatus.ok) {
        _log.severe('Failed to create session (status: $status)');
        completer.complete();
        return;
      }
      updateSession((SemanticNode session) {
        final Story newStory = new Story(sessionId,
            storyNode: sessionRoot.create(<String>[kStoryLabel]));
        _allStories.add(newStory);
        _setActiveStory(newStory, emptySession: jsonRecipe == null);
        completer.complete();
      });
    });
    return completer.future;
  }

  void _openStory(Story story) {
    _closeActiveStory();
    handlerService.restoreSession(story.sessionId,
        (final HandlerStatus status) {
      if (status != HandlerStatus.ok) {
        _log.severe('Failed to restore session (status: $status)');
        return;
      }
      // Lets assume that restore session is never a empty session.
      _setActiveStory(story, emptySession: false);
    });
  }

  // Shared a story with a user.
  void _shareStory(Story story, Person person) {
    _writeSharedSessionIdsToUser(person.email, _activeStory.sessionId);
  }

  void _updateSize(DragUpdateDetails details) {
    setState(() {
      final double maxHeight = maxModuleAreaHeight;
      final double minHeight = 100.0;
      _moduleAreaHeight = max(
          minHeight, min(maxHeight, _moduleAreaHeight + details.primaryDelta));
      _adjustStoryView();
    });
  }

  void _adjustWallpaper() {
    _wallpaperKey.currentState?.dimmed = (_isListening ||
            _composeChildren.isNotEmpty ||
            _currentSpeechText.isNotEmpty ||
            suggestionsAdapter.suggestions.isNotEmpty ||
            _allStories.isNotEmpty) &&
        !_lockscreenOverride;
  }

  void _adjustVoiceInputText() {
    if (_isListening ||
        (_currentSpeechText.isNotEmpty && _composeChildren.isEmpty)) {
      _voiceInputTextTransitionKey.currentState?.activate = true;
      _voiceInputInlineTextTransitionKey.currentState?.child = null;
    } else if (_currentSpeechText.isNotEmpty && _composeChildren.isNotEmpty) {
      _voiceInputTextTransitionKey.currentState?.activate = false;
      _voiceInputInlineTextTransitionKey.currentState?.child = new Container(
          margin: const EdgeInsets.only(
              top: 24.0, bottom: 8.0, left: 16.0, right: 16.0),
          child: new Center(child: new Text(
              '"${_currentSpeechText.toUpperCase()}"',
              style: _kInlineVoiceTextStyle,
              textAlign: TextAlign.center)));
    } else {
      _voiceInputTextTransitionKey.currentState?.activate = false;
      _voiceInputInlineTextTransitionKey.currentState?.child = null;
    }
  }

  void _adjustStoryView() {
    if (!_isListening) {
      // Module area
      _storyViewKey.currentState?.composeChildren = _composeChildren;
      if (_composeChildren.isNotEmpty) {
        _storyViewHeightKey.currentState?.height = _moduleAreaHeight;
        _nonStoryViewHeightKey.currentState?.height =
            maxModuleAreaHeight - _moduleAreaHeight;
      } else {
        _storyViewHeightKey.currentState?.height = 0.0;
        _nonStoryViewHeightKey.currentState?.height =
            maxModuleAreaHeight - 240.0;
      }
    } else {
      _storyViewHeightKey.currentState?.height = 0.0;
      _nonStoryViewHeightKey.currentState?.height = 0.0;
    }
    if (_kIncludeContextInStoryView) {
      _storyViewKey.currentState?.contextWidget = new Container(
          height: _kDeviceExtensionHeight,
          decoration: const BoxDecoration(backgroundColor: Colors.black),
          child: new Center(child: new Text(_contextString.toUpperCase(),
              style: _kContextTextStyle, textAlign: TextAlign.center)));
    }
  }

  bool get _isListening => _speechInputKey.currentState?.isListening ?? false;

  Widget _build(BuildContext context) {
    // Child widgets of the main stack above the voice input button.
    final List<Widget> stackChildren = <Widget>[
      new Wallpaper(
          key: _wallpaperKey,
          contextText: _contextString,
          contextTextStyle: _kContextTextStyle,
          activityText: _activityString,
          activityTextStyle: _kSpeechTextStyle)
    ];

    // The widgets that are drawn inside the main activity area.
    List<Widget> widgets = <Widget>[];

    // Display a screen-wide overlay if we are in listening mode.
    // TODO(armansito): Normally we would lay this over everything else but it
    // looks like z-order is messed up for ChildView widgets inside a Stack
    // (see https://github.com/flutter/flutter/issues/2637).
    widgets
        .add(new EnterExitTransition(key: _voiceInputInlineTextTransitionKey));

    final Iterable<Story> otherStories =
        _allStories?.where((Story s) => s.sessionId != _activeStory?.sessionId);

    final List<Widget> blockChildren = suggestionsAdapter != null
        ? <Widget>[
            new Container(
                margin: const EdgeInsets.only(top: 8.0),
                child: new SuggestionList(
                    suggestionsAdapter,
                    _onSelectSuggestion,
                    _openStory,
                    otherStories,
                    _liveSuggestions.values,
                    key: _suggestionListKey))
          ]
        : <Widget>[];
    if (!_isListening && _activeStory != null) {
      blockChildren.insert(
          0,
          new PeopleBarContainer(
              key: _peopleBarKey,
              getPersons: () => _shouldShowPeopleBar
                  ? _users.getOtherUsers(state)
                  : const <Person>[],
              onPersonTapped: (Person person, Rect personBounds) {
                _shareStory(_activeStory, person);
                setState(() {
                  _moduleAreaHeight = maxModuleAreaHeight;
                  _adjustStoryView();
                });
              }));
    }

    // Then all the suggestions.
    widgets.add(new Flexible(
        child:
            new Block(scrollableKey: _scrollableKey, children: blockChildren)));

    stackChildren.add(new Opacity(
        opacity: _lockscreenOverride ? 0.0 : 1.0,
        child: new Stack(children: <Widget>[
          new Positioned(
              left: 0.0,
              right: 0.0,
              top: 0.0,
              child: new ActivatableWidget(
                  key: _voiceInputTextTransitionKey,
                  child: _createVoiceInputFeedback())),
          new Positioned(
              left: 0.0,
              right: 0.0,
              top: 0.0,
              child: new SimulatedHeightWidget(
                  key: _storyViewHeightKey,
                  child: new StoryView(key: _storyViewKey))),
          new Positioned(
              left: 0.0,
              right: 0.0,
              bottom: 0.0,
              child: new SimulatedHeightWidget(
                  key: _nonStoryViewHeightKey,
                  child: new Column(children: widgets)))
        ])));

    List<Widget> outerStackChildren = <Widget>[
      new Column(children: <Widget>[
        new Flexible(child: new ConstrainedBox(
            constraints: new BoxConstraints.expand(),
            child: new Stack(children: stackChildren))),
        new Container(
            height: _kDeviceExtensionHeight,
            decoration: const BoxDecoration(backgroundColor: Colors.black))
      ])
    ];
    if (_speechInput != null) {
      outerStackChildren.add(new Positioned(
          bottom: 0.0,
          left: 0.0,
          right: 0.0,
          height: _kDeviceExtensionHeight,
          child: _speechInput));
    }

    // TODO(armansito): We can't make the GestureDetector a parent
    // of the ChildView's since it ends up getting occluded due to
    // the bug mentioned above. Instead we add a small gesture
    // area right below where the modules are. Make this nicer
    // when the bug is fixed.
    if (!_isListening && _composeChildren.isNotEmpty) {
      outerStackChildren.insert(
          1,
          new Positioned(
              top: _moduleAreaHeight,
              left: 0.0,
              right: 0.0,
              height: 32.0,
              child: new GestureDetector(
                  onVerticalDragUpdate: _updateSize,
                  behavior: HitTestBehavior.opaque)));
    }

    if (!_firstBuildCompleter.isCompleted) {
      _firstBuildCompleter.complete();
    }

    return new Stack(children: outerStackChildren);
  }

  Widget _createVoiceInputFeedback() => new Container(
      margin: const EdgeInsets.only(bottom: 8.0),
      height: 240.0,
      child: new Stack(children: <Widget>[
        new Positioned(
            top: 80.0,
            left: 0.0,
            right: 0.0,
            child: new SpeechText(
                key: _speechContextTextKey,
                style: _kContextTextStyle,
                initialText: _contextString.toUpperCase())),
        new Positioned(
            top: 120.0,
            left: 0.0,
            right: 0.0,
            child: new SpeechText(
                key: _speechTextKey,
                style: _kSpeechTextStyle,
                initialText: 'Ask for anything'))
      ]));

  @override // State
  Widget build(BuildContext context) => _build(context);

  void _onSpeechRecognized(String speechText) {
    setState(() {
      _log.info('Speech text: $speechText');
      _speechText = speechText;
      if (!_isListening) {
        _addSpeechInput(speechText);
      }
    });
  }

  set _speechText(String speechText) {
    _currentSpeechText = speechText;
    _speechTextKey.currentState?.text = _currentSpeechText.isEmpty
        ? 'Ask for anything'
        : '"$_currentSpeechText"';
    _speechContextTextKey.currentState?.text = _contextString.toUpperCase();
    _adjustVoiceInputText();
    _adjustWallpaper();
  }
}

class ActivatableWidget extends StatefulWidget {
  final Widget child;

  ActivatableWidget({Key key, this.child}) : super(key: key);

  @override
  ActivatableWidgetState createState() => new ActivatableWidgetState();
}

class ActivatableWidgetState extends State<ActivatableWidget> {
  final GlobalKey<EnterExitTransitionState> _transitionKey =
      new GlobalKey<EnterExitTransitionState>();
  bool _activated = false;

  set activate(bool activate) {
    if (activate) {
      if (!_activated) {
        _activated = true;
        _transitionKey.currentState?.child = config.child;
      }
    } else {
      if (_activated) {
        _activated = false;
        _transitionKey.currentState?.child = null;
      }
    }
  }

  @override
  Widget build(_) => new EnterExitTransition(key: _transitionKey);
}

class SpeechText extends StatefulWidget {
  final TextStyle style;
  final String initialText;
  SpeechText({Key key, this.style, this.initialText: ''}) : super(key: key);
  @override
  SpeechTextState createState() => new SpeechTextState(initialText);
}

class SpeechTextState extends State<SpeechText> {
  String _currentSpeechText = '';

  SpeechTextState(String initialText) {
    _currentSpeechText = initialText;
  }

  set text(String newText) {
    setState(() {
      _currentSpeechText = newText;
    });
  }

  @override
  Widget build(_) => new Text(_currentSpeechText,
      style: config.style, textAlign: TextAlign.center);
}

typedef List<Person> GetPersons();

class PeopleBarContainer extends StatefulWidget {
  final GetPersons getPersons;
  final PersonTappedCallback onPersonTapped;
  PeopleBarContainer({Key key, this.getPersons, this.onPersonTapped})
      : super(key: key);
  @override
  PeopleBarContainerState createState() => new PeopleBarContainerState();
}

class PeopleBarContainerState extends State<PeopleBarContainer> {
  @override
  Widget build(_) => new PeopleBar(
      people: config.getPersons(), onPersonTapped: config.onPersonTapped);
}

void main() {
  new FlutterModule.withState(() => new LauncherState());
}
