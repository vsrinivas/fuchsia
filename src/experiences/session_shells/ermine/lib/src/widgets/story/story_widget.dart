// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:built_collection/built_collection.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_scenic_flutter/child_view_connection.dart'
    show ChildViewConnection;
import 'package:tiler/tiler.dart';
import 'package:story_shell_labs_lib/layout/tile_model.dart';
import 'package:story_shell_labs_lib/layout/tile_presenter.dart';

import 'remove_button_target_widget.dart';

final List<Color> _kColors = [
  Colors.red,
  Colors.blue,
  Colors.yellow,
  Colors.green,
  Colors.pink,
  Colors.orange,
  Colors.purple,
];

class StoryWidget extends StatefulWidget {
  final TilePresenter presenter;
  final ValueNotifier<bool> confirmEdit;
  final bool editing;

  const StoryWidget({
    @required this.presenter,
    this.confirmEdit,
    this.editing = false,
  });

  @override
  _StoryWidgetState createState() => _StoryWidgetState();
}

class _StoryWidgetState extends State<StoryWidget> {
  /// Used for resizing locally, moving, etc when in edit mode.
  /// Once out of edit mode, LayoutBloc is notified with the updated model.
  TilerModel<ModuleInfo> _tilerModel;
  BuiltMap<String, ChildViewConnection> _connections;
  StreamSubscription _tilerUpdateListener;
  bool _isEditing = false;
  OverlayEntry _layoutSuggestionsOverlay;
  Map<String, Color> _parametersToColors;
  final ValueNotifier _focusedMod = ValueNotifier<String>(null);

  @override
  void initState() {
    _resetTilerModel();
    _isEditing = widget.editing;
    widget.confirmEdit.addListener(_confirmEditListener);
    _tilerUpdateListener = widget.presenter.update.listen((update) {
      setState(() {
        _isEditing = false;
        _resetTilerModel(update: update);
      });
      updateLayoutSuggestionsOverlayVisibility();
    });
    super.initState();
  }

  @override
  void didUpdateWidget(StoryWidget oldWidget) {
    super.didUpdateWidget(oldWidget);
    _isEditing = widget.editing;
    oldWidget.confirmEdit.removeListener(_confirmEditListener);
    widget.confirmEdit.addListener(_confirmEditListener);
    if (oldWidget.presenter != widget.presenter) {
      _resetTilerModel();
      _tilerUpdateListener.cancel();
      _tilerUpdateListener = widget.presenter.update.listen((update) {
        setState(() {
          _isEditing = false;
          _resetTilerModel(update: update);
        });
        updateLayoutSuggestionsOverlayVisibility();
      });
    }
  }

  @override
  void dispose() {
    _tilerUpdateListener.cancel();
    super.dispose();
  }

  void _resetTilerModel({TileLayoutModel update}) {
    update ??= widget.presenter.currentState;
    _tilerModel = update.model;
    _connections = update.connections;
    _parametersToColors = _mapFromKeysAndCircularValues(
      _allParametersInModel(_tilerModel),
      _kColors,
    );
  }

  Iterable<ModuleInfo> _flattenTileModel(TileModel tile) => tile == null
      ? []
      : (tile.tiles.expand(_flattenTileModel).toList()..add(tile.content));

  Iterable<String> _allParametersInModel(TilerModel model) =>
      _flattenTileModel(model.root)
          .expand((ModuleInfo content) => content?.parameters ?? <String>[])
          .toSet();

  Map<K, V> _mapFromKeysAndCircularValues<K, V>(
    Iterable<K> keys,
    Iterable<V> values,
  ) =>
      Map.fromIterables(
        keys,
        List.generate(keys.length, (i) => values.elementAt(i % values.length)),
      );

  @override
  Widget build(BuildContext context) {
    return Stack(
      children: <Widget>[
        Positioned.fill(
          child: LayoutPresenter(
            tilerModel: _tilerModel,
            connections: _connections,
            isEditing: _isEditing,
            focusedMod: _focusedMod,
            parametersToColors: _parametersToColors,
            setTilerModel: (model) {
              setState(() {
                _tilerModel = cloneTiler(model);
              });
            },
          ),
        ),
      ],
    );
  }

  void _endEditing() {
    widget.presenter.requestLayout(_tilerModel);
  }

  void _cancelEditing() {
    setState(() {
      _isEditing = false;
      _resetTilerModel();
    });
    updateLayoutSuggestionsOverlayVisibility();
  }

  void _confirmEditListener() {
    if (_isEditing) {
      return;
    }
    if (widget.confirmEdit.value) {
      _endEditing();
    } else {
      _cancelEditing();
    }
  }

  void updateLayoutSuggestionsOverlayVisibility() {
    if (_isEditing && _layoutSuggestionsOverlay == null) {
      _layoutSuggestionsOverlay = OverlayEntry(
        builder: (context) {
          return Positioned(
            left: 0,
            right: 0,
            bottom: 8,
            child: Align(
              alignment: Alignment.bottomCenter,
              child: SizedBox(
                height: 32,
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: <Widget>[
                    LayoutSuggestionsWidget(
                      presenter: widget.presenter,
                      focusedMod: _focusedMod,
                      onSelect: (model) {
                        setState(() {
                          _tilerModel = cloneTiler(model);
                        });
                      },
                    ),
                    RemoveButtonTargetWidget(
                      onTap: () {
                        getTileContent(_tilerModel)
                            .where((TileModel tile) =>
                                tile.content.modName == _focusedMod.value)
                            .forEach(_tilerModel.remove);
                      },
                    ),
                  ],
                ),
              ),
            ),
          );
        },
      );
      Overlay.of(context).insert(_layoutSuggestionsOverlay);
    }
    if (!_isEditing && _layoutSuggestionsOverlay != null) {
      _layoutSuggestionsOverlay.remove();
      _layoutSuggestionsOverlay = null;
    }
  }
}
