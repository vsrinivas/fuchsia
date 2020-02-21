// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:internationalization/strings.dart';
import '../blocs/tabs_bloc.dart';
import '../blocs/webpage_bloc.dart';
import '../models/tabs_action.dart';

// TODO(fxb/45264): Make the common factors as part of Ermine central styles.
const _kTabBarHeight = 24.0;
const _kMinTabWidth = 120.0;
const _kBorderWidth = 1.0;
const _kTabPadding = EdgeInsets.symmetric(horizontal: _kTabBarHeight);
const _kScrollToMargin = _kMinTabWidth / 3;
const _kScrollAnimationDuration = 300;
const _kIconSize = 14.0;

enum _ScrollButtonType { left, right }

@visibleForTesting
double get kTabBarHeight => _kTabBarHeight;

@visibleForTesting
double get kMinTabWidth => _kMinTabWidth;

@visibleForTesting
double get kBorderWidth => _kBorderWidth;

@visibleForTesting
double get kScrollToMargin => _kScrollToMargin;

/// The list of currently opened tabs in the browser.
///
/// Builds different widget trees for the tab list depending on the selected tab
/// and the number of tabs.
/// Handles tab rearrangement and list scroll events, and is also involved in drawing
/// tab borders as they are affected by the tab rearrangement action.
class TabsWidget extends StatefulWidget {
  final TabsBloc bloc;
  const TabsWidget({@required this.bloc});

  @override
  _TabsWidgetState createState() => _TabsWidgetState();
}

class _TabsWidgetState extends State<TabsWidget>
    with TickerProviderStateMixin<TabsWidget> {
  double _tabListWidth = 0.0;
  double _tabWidth = 0.0;
  double _dragStartX = 0.0;
  final _currentTabX = ValueNotifier<double>(0.0);

  int _ghostIndex = 0;
  int _dragStartIndex = 0;
  bool _isDragging;
  bool _isAnimating;

  static const Duration _reorderAnimationDuration = Duration(milliseconds: 200);
  AnimationController _ghostController;
  AnimationController _leftNewGhostController;
  AnimationController _rightNewGhostController;

  final _scrollController = ScrollController();
  final _leftScrollButton = _ScrollButton(_ScrollButtonType.left);
  final _rightScrollButton = _ScrollButton(_ScrollButtonType.right);

  ThemeData _browserTheme;

  @override
  void initState() {
    super.initState();
    _isDragging = false;
    _isAnimating = false;

    _ghostController =
        AnimationController(vsync: this, duration: _reorderAnimationDuration);
    _leftNewGhostController =
        AnimationController(vsync: this, duration: _reorderAnimationDuration);
    _rightNewGhostController =
        AnimationController(vsync: this, duration: _reorderAnimationDuration);

    _ghostController.value = 1.0;
    _leftNewGhostController.value = 0.0;
    _rightNewGhostController.value = 0.0;

    _leftNewGhostController.addStatusListener(_onLeftNewGhostControllerChanged);
    _rightNewGhostController
        .addStatusListener(_onRightNewGhostControllerChanged);
    _setupBloc(null, widget);
  }

  @override
  void dispose() {
    _setupBloc(widget, null);
    _ghostController.dispose();
    _leftNewGhostController
      ..removeStatusListener(_onLeftNewGhostControllerChanged)
      ..dispose();
    _rightNewGhostController
      ..removeStatusListener(_onRightNewGhostControllerChanged)
      ..dispose();
    _scrollController.dispose();
    super.dispose();
  }

  @override
  void didUpdateWidget(TabsWidget oldWidget) {
    super.didUpdateWidget(oldWidget);
    _setupBloc(oldWidget, widget);
  }

  void _setupBloc(TabsWidget oldWidget, TabsWidget newWidget) {
    if (oldWidget?.bloc != newWidget?.bloc) {
      oldWidget?.bloc?.currentTabNotifier?.removeListener(_onCurrentTabChanged);
      widget?.bloc?.currentTabNotifier?.addListener(_onCurrentTabChanged);
      oldWidget?.bloc?.tabsNotifier?.removeListener(_onTabsChanged);
      widget?.bloc?.tabsNotifier?.addListener(_onTabsChanged);
    }
  }

  @override
  Widget build(BuildContext context) {
    _tabListWidth = MediaQuery.of(context).size.width;
    _browserTheme = Theme.of(context);

    return AnimatedBuilder(
        animation: Listenable.merge(
            [widget.bloc.tabsNotifier, widget.bloc.currentTabNotifier]),
        builder: (_, __) {
          if (widget.bloc.tabs.length > 1) {
            _setVariableTabWidth();
            return Container(
              height: _kTabBarHeight,
              decoration: BoxDecoration(
                color: _browserTheme.primaryColor,
                border: Border(
                  top: BorderSide(
                    color: _browserTheme.accentColor,
                    width: _kBorderWidth,
                  ),
                  bottom: BorderSide(
                    color: _browserTheme.accentColor,
                    width: _kBorderWidth,
                  ),
                ),
              ),
              child: LayoutBuilder(
                builder: (context, constraints) => _tabWidth > _kMinTabWidth
                    ? _buildTabStacks()
                    : _buildScrollableTabListWithButtons(),
              ),
            );
          }
          return Offstage();
        });
  }

  void _setVariableTabWidth() {
    _tabWidth = (_tabListWidth / widget.bloc.tabs.length)
        .clamp(_kMinTabWidth, _tabListWidth / 2);
    if (!_isDragging) {
      _currentTabX.value = _tabWidth * widget.bloc.currentTabIdx;
    }
  }

  // BUILDERS

  Widget _buildScrollableTabListWithButtons() => Row(
        children: <Widget>[
          _buildScrollButton(_leftScrollButton),
          Expanded(child: _buildScrollableTabList()),
          _buildScrollButton(_rightScrollButton),
        ],
      );

  Widget _buildScrollableTabList() => NotificationListener<ScrollNotification>(
        onNotification: (scrollNotification) {
          if (scrollNotification is ScrollEndNotification) {
            _onScrollEnd(scrollNotification.metrics);
          }
          return true;
        },
        child: SingleChildScrollView(
          controller: _scrollController,
          scrollDirection: Axis.horizontal,
          physics: NeverScrollableScrollPhysics(),
          child: _buildTabStacks(),
        ),
      );

  Widget _buildScrollButton(_ScrollButton button) => GestureDetector(
        onTap: () => _onScrollButtonTap(button),
        child: Container(
          width: _kTabBarHeight,
          height: _kTabBarHeight,
          color: _browserTheme.buttonColor,
          child: Center(
            child: AnimatedBuilder(
              animation: button.isEnabled,
              builder: (_, __) => Icon(
                button.icon,
                color: button.isEnabled.value
                    ? _browserTheme.primaryColor
                    : _browserTheme.primaryColor.withOpacity(0.2),
                size: _kIconSize,
              ),
            ),
          ),
        ),
      );

  Widget _buildTabStacks() => Stack(
        children: <Widget>[
          Row(
            children: List.generate(widget.bloc.tabs.length, (index) {
              return _wrapWithGestureDetector(
                index,
                _buildUnselectedTab(index),
              );
            }),
          ),
          AnimatedBuilder(
            animation: _currentTabX,
            builder: (_, __) => Positioned(
              left: _currentTabX.value,
              child: _wrapWithGestureDetector(
                widget.bloc.currentTabIdx,
                _buildTabWithBorder(widget.bloc.currentTabIdx),
              ),
            ),
          ),
        ],
      );

  // Makes a tab to be rearrangeable and selectable.
  Widget _wrapWithGestureDetector(int index, Widget child) => GestureDetector(
        onHorizontalDragStart: (DragStartDetails details) =>
            _onDragStart(index, details),
        onHorizontalDragUpdate: _onDragUpdate,
        onHorizontalDragEnd: _onDragEnd,
        child: child,
      );

  Widget _buildUnselectedTab(int index) {
    final spacing = Container(
      width: _tabWidth,
      height: _kTabBarHeight,
      decoration: BoxDecoration(
        border: _buildBorder(index != 0),
      ),
    );

    if (index == _ghostIndex) {
      return _buildGhostTab(_ghostController, spacing);
    }

    int actualIndex = index;

    // Shifts the tabs located between the moving tab's original and current positions
    //  to the right if the it is moving to the left.
    if (_ghostIndex < widget.bloc.currentTabIdx) {
      //
      if (index > _ghostIndex && index <= widget.bloc.currentTabIdx) {
        actualIndex = index - 1;
      }
    }
    // Shifts the tabs located between the moving tab's original and current positions
    // to the left if it is moving to the right.
    else if (_ghostIndex > widget.bloc.currentTabIdx) {
      if (index < _ghostIndex && index >= widget.bloc.currentTabIdx) {
        actualIndex = index + 1;
      }
    }

    final child = _buildTabWithBorder(actualIndex, renderingIndex: index);

    // Inserts a potential empty space to the left of the tab which is currently left
    // to the moving tab.
    if (index == _ghostIndex - 1) {
      return Row(
        children: [
          _buildGhostTab(_leftNewGhostController, spacing),
          child,
        ],
      );
    }
    // Inserts a potential empty space to the right of the tab which is currently right
    // to the moving tab.
    else if (index == _ghostIndex + 1) {
      return Row(
        children: [
          child,
          _buildGhostTab(_rightNewGhostController, spacing),
        ],
      );
    }

    return child;
  }

  Widget _buildTabWithBorder(int index, {int renderingIndex}) {
    renderingIndex ??= index;

    return Container(
      key: Key('tab'),
      width: _tabWidth,
      height: _kTabBarHeight,
      decoration: BoxDecoration(
        color: (index == widget.bloc.currentTabIdx)
            ? _browserTheme.accentColor
            : _browserTheme.primaryColor,
        border: _buildBorder(index != widget.bloc.currentTabIdx &&
            !((!_isAnimating) && renderingIndex == 0) &&
            !(_isAnimating &&
                (renderingIndex != _ghostIndex - 1 && renderingIndex == 0))),
      ),
      child: _buildTab(widget.bloc.tabs[index]),
    );
  }

  // Creates an empty space for the selected tab on the unselected tab widget list.
  Widget _buildGhostTab(
          AnimationController animationController, Widget child) =>
      SizeTransition(
        sizeFactor: animationController,
        axis: Axis.horizontal,
        axisAlignment: -1.0,
        child: child,
      );

  Border _buildBorder(bool hasBorder) => Border(
        left: BorderSide(
          color: hasBorder ? _browserTheme.accentColor : Colors.transparent,
          width: _kBorderWidth,
        ),
      );

  Widget _buildTab(WebPageBloc tab) => _TabWidget(
        bloc: tab,
        selected: tab == widget.bloc.currentTab,
        onSelect: () => widget.bloc.request.add(FocusTabAction(tab: tab)),
        onClose: () => widget.bloc.request.add(RemoveTabAction(tab: tab)),
      );

  // EVENT HANDLERS

  void _onTabsChanged() {
    _syncGhost();
  }

  void _onCurrentTabChanged() {
    _syncGhost();

    if (_scrollController.hasClients) {
      final viewportWidth = _scrollController.position.viewportDimension;
      final currentTabIndex = widget.bloc.currentTabIdx;
      final currentTabPosition = currentTabIndex * _kMinTabWidth;

      final offsetForLeftEdge = currentTabPosition - _kScrollToMargin;
      final offsetForRightEdge =
          currentTabPosition - viewportWidth + _kMinTabWidth + _kScrollToMargin;

      double newOffset;

      if (_scrollController.offset > offsetForLeftEdge) {
        newOffset = offsetForLeftEdge;
      } else if (_scrollController.offset < offsetForRightEdge) {
        newOffset = offsetForRightEdge;
      }

      if (newOffset != null) {
        _scrollController.animateTo(
          newOffset,
          duration: Duration(milliseconds: _kScrollAnimationDuration),
          curve: Curves.ease,
        );
      }
    }
  }

  void _syncGhost() {
    _ghostIndex = widget.bloc.currentTabIdx;
    _currentTabX.value = _tabWidth * _ghostIndex;
  }

  void _onDragStart(int index, DragStartDetails details) {
    if (index != widget.bloc.currentTabIdx) {
      widget.bloc.request.add(FocusTabAction(tab: widget.bloc.tabs[index]));
    }
    _isDragging = true;
    _dragStartIndex = index;
    _ghostIndex = index;
    _dragStartX = details.globalPosition.dx;
  }

  void _onDragUpdate(DragUpdateDetails details) {
    double dragOffsetX = details.globalPosition.dx - _dragStartX;
    double dragXMax = (_scrollController.hasClients)
        ? (_tabWidth * widget.bloc.tabs.length) - _tabWidth
        : (_tabListWidth - _tabWidth);
    _currentTabX.value = ((_tabWidth * widget.bloc.currentTabIdx) + dragOffsetX)
        .clamp(0.0, dragXMax);

    if (!_isAnimating) {
      if (_isOverlappingLeftTabHalf()) {
        _shiftLeftToRight();
      }
      if (_isOverlappingRightTabHalf()) {
        _shiftRightToLeft();
      }
    }
  }

  void _onDragEnd(DragEndDetails details) {
    _isDragging = false;

    // Rearranges the selected tab to the currently empty space only when there is no
    // unfinished animations.
    if (!_isAnimating) {
      _completeRearrangement();
    }
  }

  void _onLeftNewGhostControllerChanged(AnimationStatus status) {
    if (status == AnimationStatus.completed) {
      _isAnimating = false;
      --_ghostIndex;
      _ghostController.value = 1.0;
      _leftNewGhostController.value = 0.0;

      _onAnimationInterrupted();
    }
  }

  void _onRightNewGhostControllerChanged(AnimationStatus status) {
    if (status == AnimationStatus.completed) {
      _isAnimating = false;
      ++_ghostIndex;
      _ghostController.value = 1.0;
      _rightNewGhostController.value = 0.0;

      _onAnimationInterrupted();
    }
  }

  // Checks if the DragEnd event occurs before a spacing;s size transition animation
  // finishes, and if so, rearranges the selected tab to the currently empty space.
  void _onAnimationInterrupted() {
    if (_isDragging == false) {
      _completeRearrangement();
    }
  }

  void _completeRearrangement() {
    if (_ghostIndex != _dragStartIndex) {
      widget.bloc.request.add(RearrangeTabsAction(
        originalIndex: _dragStartIndex,
        newIndex: _ghostIndex,
      ));
    }
    _currentTabX.value = _tabWidth * _ghostIndex;
  }

  void _onScrollButtonTap(_ScrollButton button) {
    if (!button.isEnabled.value) {
      return;
    }

    final currentOffset = _scrollController.offset;
    final newOffset = (_tabListWidth / 2) * button.directionFactor;

    _scrollController.animateTo(
      currentOffset + newOffset,
      duration: Duration(milliseconds: _kScrollAnimationDuration),
      curve: Curves.ease,
    );
  }

  void _onScrollEnd(ScrollMetrics metrics) {
    if (_canScrollTo(_ScrollButtonType.left)) {
      _leftScrollButton.enable();
    } else {
      _leftScrollButton.disable();
    }

    if (_canScrollTo(_ScrollButtonType.right)) {
      _rightScrollButton.enable();
    } else {
      _rightScrollButton.disable();
    }
  }

  // CHECKERS

  bool _isOverlappingLeftTabHalf() {
    if (_ghostIndex < 1) {
      return false;
    }

    double leftTabCenterX = _tabWidth * (_ghostIndex - 1) + (_tabWidth / 2);
    if (_currentTabX.value < leftTabCenterX) {
      return true;
    }
    return false;
  }

  bool _isOverlappingRightTabHalf() {
    if (_ghostIndex > widget.bloc.tabs.length - 2) {
      return false;
    }
    double rightTabCenterX = _tabWidth * (_ghostIndex + 1) + (_tabWidth / 2);
    if (_currentTabX.value + _tabWidth > rightTabCenterX) {
      return true;
    }
    return false;
  }

  bool _canScrollTo(_ScrollButtonType direction) {
    switch (direction) {
      case _ScrollButtonType.left:
        if (_scrollController.offset <= 0.0) {
          return false;
        }
        return true;
      case _ScrollButtonType.right:
        if (_scrollController.offset >=
            (_kMinTabWidth * widget.bloc.tabs.length -
                _scrollController.position.viewportDimension)) {
          return false;
        }
        return true;
      default:
        return true;
    }
  }

  // ANIMATORS

  void _shiftLeftToRight() {
    _isAnimating = true;
    _ghostController.reverse(from: 1.0);
    _leftNewGhostController.forward(from: 0.0);
  }

  void _shiftRightToLeft() {
    _isAnimating = true;
    _ghostController.reverse(from: 1.0);
    _rightNewGhostController.forward(from: 0.0);
  }
}

/// An individual tab.
///
/// Shows the title of its webpage and displays a'close' button when it is selected or
/// a mouse cursor hovers over it. Also, handles the closing tab event when the close
/// button is tapped on.
class _TabWidget extends StatefulWidget {
  const _TabWidget({this.bloc, this.selected, this.onSelect, this.onClose});
  final WebPageBloc bloc;
  final bool selected;
  final VoidCallback onSelect;
  final VoidCallback onClose;

  @override
  _TabWidgetState createState() => _TabWidgetState();
}

class _TabWidgetState extends State<_TabWidget> {
  final _hovering = ValueNotifier<bool>(false);
  @override
  Widget build(BuildContext context) {
    final baseTheme = Theme.of(context);
    return MouseRegion(
      onEnter: (_) {
        _hovering.value = true;
      },
      onExit: (_) {
        WidgetsBinding.instance.addPostFrameCallback((_) {
          _hovering.value = false;
        });
      },
      // TODO(fxb/45239): Remove GestureDetector and Container.
      child: GestureDetector(
        onTap: widget.onSelect,
        child: Container(
          color:
              widget.selected ? baseTheme.accentColor : baseTheme.primaryColor,
          child: DefaultTextStyle(
            style: baseTheme.textTheme.bodyText2.copyWith(
              color: widget.selected
                  ? baseTheme.primaryColor
                  : baseTheme.accentColor,
            ),
            child: Stack(
              children: <Widget>[
                Center(
                  child: Padding(
                    padding: _kTabPadding,
                    child: AnimatedBuilder(
                      animation: widget.bloc.pageTitleNotifier,
                      builder: (_, __) => Text(
                        widget.bloc.pageTitle ?? Strings.newtab.toUpperCase(),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                    ),
                  ),
                ),
                Positioned(
                  right: 0.0,
                  child: AnimatedBuilder(
                    animation: _hovering,
                    builder: (_, child) => Offstage(
                      offstage: !(widget.selected || _hovering.value),
                      child: child,
                    ),
                    child: Padding(
                      padding: EdgeInsets.all(4.0),
                      child: GestureDetector(
                        onTap: widget.onClose,
                        child: Container(
                          color: Colors.transparent,
                          alignment: Alignment.center,
                          child: Icon(
                            Icons.clear,
                            color: widget.selected
                                ? baseTheme.primaryColor
                                : baseTheme.accentColor,
                            size: _kIconSize,
                          ),
                        ),
                      ),
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _ScrollButton {
  final _ScrollButtonType type;
  IconData icon;
  final ValueNotifier<bool> isEnabled = ValueNotifier<bool>(true);
  double directionFactor = 0.0;

  _ScrollButton(this.type)
      : assert(
            type == _ScrollButtonType.left || type == _ScrollButtonType.right) {
    switch (type) {
      case _ScrollButtonType.left:
        icon = Icons.keyboard_arrow_left;
        directionFactor = -1.0;
        break;
      case _ScrollButtonType.right:
        icon = Icons.keyboard_arrow_right;
        directionFactor = 1.0;
        break;
    }
  }

  void disable() => isEnabled.value = false;
  void enable() => isEnabled.value = true;
}
