// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:common/uuid_mojo_helpers.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:modular_flutter/flutter_module.dart' show ModuleComposeData;
import 'package:modular_core/uuid.dart' show Uuid;
import 'package:modular_services/suggestinator/suggestions.mojom.dart' as mojom;

import 'story.dart';
import 'suggestions_adapter.dart';
import 'ui/spring_column.dart';

const double _kItemExtent = 96.0;

typedef void SelectSuggestionCallback(
    String sessionId, String suggestionDescription);
typedef void OpenStoryCallback(Story story);

const TextStyle _kSuggestionTitleTextStyle = const TextStyle(
    fontSize: 16.0,
    fontWeight: FontWeight.w400, // regular
    fontFamily: 'Roboto');
const TextStyle _kSublineTextStyle = const TextStyle(
    fontSize: 10.0,
    fontWeight: FontWeight.w900,
    fontFamily: 'Roboto',
    color: const Color(0x80FFFFFF),
    letterSpacing: 1.5);
const double _kSuggestionIconDim = 54.0;

class RoundRobinColorVendor {
  static final List<Color> _itemColors = <Color>[
    Colors.pink[400],
    Colors.lightBlue[400],
    Colors.green[400]
  ];
  int _index = 0;

  Color get current => _itemColors[_index];
  Color get next {
    _index = (_index + 1) % _itemColors.length;
    return current;
  }
}

class LiveSuggestionData {
  final Uuid suggestionId;
  final Widget widget;
  final ModuleComposeData composeData;

  LiveSuggestionData(this.suggestionId, this.widget, this.composeData);
}

// A widget that knows how to render a list of context suggestions.
class SuggestionList extends StatefulWidget {
  SuggestionList(this.adapter, this.selectSuggestion, this.openStory,
      this.otherStories, this.liveSuggestions,
      {GlobalKey<SuggestionListState> key})
      : super(key: key);

  final SelectSuggestionCallback selectSuggestion;
  final OpenStoryCallback openStory;
  final SuggestionsAdapter adapter;

  // Session ids started by the user or shared with this user. Used to show
  // suggestions to the user to restore those sessions.
  final Iterable<Story> otherStories;

  // Composed widgets obtained for suggestions that are being run speculatively.
  // TODO(armansito): We need to also carry the suggestion ID here
  // somehow, so that the suggestion list can show this instead of the
  // suggestion's basic embodiment, as well as know how to correctly
  // intercept a tap event.
  final Iterable<LiveSuggestionData> liveSuggestions;

  @override
  SuggestionListState createState() => new SuggestionListState();
}

typedef void PickSuggestionFunction(final mojom.Suggestion suggestion);

class SuggestionListState extends State<SuggestionList> {
  @override // State
  void initState() {
    super.initState();
    config.adapter.addListener(onSuggestionsUpdated);
  }

  @override // State
  void dispose() {
    config.adapter.removeListener(onSuggestionsUpdated);
    super.dispose();
  }

  @override // State
  Widget build(BuildContext context) {
    return new SpringColumn(children: _buildListItems());
  }

  void onSuggestionsUpdated() {
    setState(() {});
  }

  Future<Null> _selectSuggestion(final mojom.Suggestion s) async {
    final String sessionId = await config.adapter.selectSuggestion(s);
    if (config.selectSuggestion != null) {
      config.selectSuggestion(sessionId, s.description);
    }
  }

  List<Widget> _buildListItems() {
    final RoundRobinColorVendor backgroundColorVendor =
        new RoundRobinColorVendor();

    // Add the composed live suggestions first.
    final List<SuggestionListItem> listItems = config.liveSuggestions
        .map((final LiveSuggestionData data) =>
            new SuggestionListItem.fromLiveSuggestion(data, () {
              final mojom.Suggestion s = config.adapter.suggestions.firstWhere(
                  (final mojom.Suggestion s) =>
                      UuidMojoHelpers.fromMojom(s.id) == data.suggestionId);
              assert(s != null);
              _selectSuggestion(s);
            }))
        .toList();

    // Add suggestions from the Suggestinator that have no live data.
    listItems.addAll(config.adapter.suggestions
        .where((final mojom.Suggestion s) => !config.liveSuggestions.any(
            (final LiveSuggestionData data) =>
                data.suggestionId == UuidMojoHelpers.fromMojom(s.id)))
        .map((final mojom.Suggestion s) {
      final Color backgroundColor = s.themeColor < 0
          ? backgroundColorVendor.next
          : new Color(s.themeColor).withAlpha(255);
      return new SuggestionListItem(
          s.description, backgroundColor, s.iconUrl, () => _selectSuggestion(s),
          subTitle: s.createsNewSession ? 'create a new story' : '',
          key: new ValueKey<String>(s.id.value.toString()));
    }));

    // TODO(armansito): Everything added to |listItems| below here should be in
    // a different UI segment as placing these among context suggestions is
    // confusing.
    config.otherStories?.forEach((final Story story) {
      listItems.add(new SuggestionListItem(
          story.title, Colors.grey[500], null, () => config.openStory(story),
          subTitle: 'open your story',
          key: new ValueKey<String>(story.sessionId)));
    });

    return listItems;
  }
}

class SuggestionListItem extends StatelessWidget {
  final String _description;
  final String _iconUrl;
  final String subTitle;
  final Color _color;
  final Function _action;

  final LiveSuggestionData _liveSuggestion;

  const SuggestionListItem(
      this._description, this._color, this._iconUrl, this._action,
      {this.subTitle: '', Key key})
      : _liveSuggestion = null,
        super(key: key);

  const SuggestionListItem.fromLiveSuggestion(
      this._liveSuggestion, this._action,
      {Key key})
      : _description = null,
        _iconUrl = null,
        subTitle = null,
        _color = null,
        super(key: key);

  Uri get iconUrl => _iconUrl == null ? null : Uri.parse(_iconUrl);

  @override // StatelessWidget
  Widget build(BuildContext context) {
    Widget iconChild;
    if (_iconUrl != null) {
      // TODO(armansito): I originally intended to use a ClipRRect here to clip
      // the icon within a circle but that really impaired performance. So we
      // have to require that the icon image itself is cropped to a circle
      // shape.
      iconChild =
          new Image(image: new NetworkImage('$iconUrl'), fit: ImageFit.cover);
    }
    Widget child;
    if (_liveSuggestion != null) {
      // TODO(armansito): Have to add a clickable area next to the suggestion
      // widget here since the GestureDetector that we add below cannot
      // intercept touches on a ChildView (see
      // https://github.com/flutter/flutter/issues/2637)
      child = new Row(children: <Widget>[
        new Container(
            decoration: new BoxDecoration(backgroundColor: Colors.white),
            width: 20.0),
        new Flexible(child: _liveSuggestion.widget)
      ]);
    } else {
      child = new Stack(children: <Widget>[
        new Align(
            alignment: const FractionalOffset(0.0, 0.5),
            child: new Container(
                margin: const EdgeInsets.only(left: 18.0),
                width: _kSuggestionIconDim,
                height: _kSuggestionIconDim,
                decoration: const BoxDecoration(
                    backgroundColor: const Color(0x33000000),
                    shape: BoxShape.circle),
                child: iconChild)),
        new Align(
            alignment: const FractionalOffset(0.0, 0.5),
            child: new Container(
                margin: const EdgeInsets.only(left: 88.0),
                child: new Column(
                    mainAxisSize: MainAxisSize.min,
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: <Widget>[
                      new Text(_description,
                          style: _kSuggestionTitleTextStyle,
                          textAlign: TextAlign.left),
                      new Container(height: 8.0),
                      new Text(subTitle.toUpperCase(),
                          style: _kSublineTextStyle, textAlign: TextAlign.left)
                    ])))
      ]);
    }
    return new Padding(
        padding: const EdgeInsets.symmetric(vertical: 8.0, horizontal: 16.0),
        child: new GestureDetector(
            onTap: _action,
            child: new Container(
                height: _kItemExtent,
                decoration: new BoxDecoration(
                    borderRadius: new BorderRadius.circular(10.0),
                    backgroundColor: _color),
                child: child)));
  }
}
