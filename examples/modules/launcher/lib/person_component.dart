// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart' as shadows;
import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';
import 'package:representation_types/person.dart';

const Color turquoiseColor = const Color(0xFF009999);
const Color turquoiseAccentColor = const Color(0xFF68EFAD);
const double personPadding = 16.0;
const double halfPersonPadding = personPadding / 2.0;
const double personDiameter = 48.0;

const double _kPersonSizeMultiple = 1.5;
const double _kPersonBorderWidth = 1.0;

typedef void PersonLongPressedCallback(Person person, Point longPressLocation);
typedef void PersonTappedCallback(Person person, Rect personBounds);

/// Represents a [Person] by displaying the circular image.
class PersonComponent extends StatelessWidget {
  final Person person;
  final bool applyPadding;
  final bool first;
  final bool last;
  final PersonTappedCallback onPersonTapped;
  final List<BoxShadow> boxShadow;
  final double sizeMultiple;
  final double borderWidth;

  /// If [applyPadding] is false, the person will have no padding around it.
  /// If [first] is true, the person will have  extra padding to its left.
  /// If [last] is true, the person will have extra padding to its right.
  /// [onPersonTapped] is called when the person is tapped.
  /// [boxShadow] set to non-null to apply shadows to the person.
  /// The visual size of the [Person] is multiplied by the [sizeMultiple].
  /// If [borderWidth] is non-zero the [Person] will be given a border of that
  /// width.
  const PersonComponent(this.person,
      {this.applyPadding: false,
      this.first: false,
      this.last: false,
      this.onPersonTapped: null,
      this.boxShadow: null,
      this.sizeMultiple: 1.0,
      this.borderWidth: null});

  @override
  Widget build(BuildContext context) {
    Offset selectedOffset = _getPersonOriginOffset(_kPersonSizeMultiple);
    return new LongPressDraggable<Person>(
        dragAnchor: DragAnchor.pointer,
        feedbackOffset:
            selectedOffset + _getPersonCenterOffset(_kPersonSizeMultiple),
        data: person,
        child: _createPerson(context, person,
            applyPadding: applyPadding,
            first: first,
            last: last,
            onPersonTapped: onPersonTapped,
            boxShadow: boxShadow,
            sizeMultiple: sizeMultiple,
            borderWidth: borderWidth),
        feedback: new Transform(
            transform: new Matrix4.identity()
              ..translate(selectedOffset.dx, selectedOffset.dy),
            child: _createPerson(context, person,
                boxShadow: shadows.kElevationToShadow[2])));
  }

  Widget _createPerson(final BuildContext context, final Person person,
      {bool applyPadding: false,
      bool first: false,
      bool last: false,
      PersonTappedCallback onPersonTapped,
      final List<BoxShadow> boxShadow,
      double sizeMultiple: _kPersonSizeMultiple,
      double borderWidth: _kPersonBorderWidth}) {
    Widget personContainer = new Container(
        decoration:
            const BoxDecoration(backgroundColor: shadows.Colors.white70),
        child: (person.avatarUrl == null || person.avatarUrl.isEmpty)
            ? new shadows.Icon(shadows.Icons.person,
                color: shadows.Colors.black87, size: 32.0)
            : new Image(
                image: new NetworkImage(person.avatarUrl),
                fit: ImageFit.cover));
    Widget ovalChild = (onPersonTapped == null)
        ? personContainer
        : new GestureDetector(
            onTap: () {
              final RenderBox renderBox = context.findRenderObject();
              onPersonTapped(
                  person,
                  (renderBox.localToGlobal(Point.origin) +
                          const Offset(-personPadding, personPadding)) &
                      new Size((personDiameter * sizeMultiple) + personPadding,
                          (personDiameter * sizeMultiple) + personPadding));
            },
            child: personContainer);
    return new Container(
        width: personDiameter * sizeMultiple,
        height: personDiameter * sizeMultiple,
        decoration: new BoxDecoration(
            border: borderWidth == null
                ? null
                : new Border.all(
                    width: borderWidth, color: turquoiseAccentColor),
            shape: BoxShape.circle,
            boxShadow: boxShadow),
        margin: applyPadding
            ? new EdgeInsets.only(
                left: first ? personPadding : personPadding,
                right: last ? personPadding : 0.0)
            : const EdgeInsets.only(),
        child: new ClipOval(child: ovalChild));
  }

  Offset _getPersonOriginOffset(double personSizeMultiple) => new Offset(
      -(personDiameter * personSizeMultiple / 2.0),
      -(personDiameter / 2.0) * (1.0 + personSizeMultiple) -
          (personDiameter * personSizeMultiple / 2.0));

  Offset _getPersonCenterOffset(double personSizeMultiple) => new Offset(
      personDiameter * personSizeMultiple / 2.0,
      personDiameter * personSizeMultiple / 2.0);
}
