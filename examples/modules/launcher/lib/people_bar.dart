// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/widgets.dart';
import 'package:representation_types/person.dart';

import 'person_component.dart';

/// Represent a list of [Person]s in a horizontal list of [PersonComponent]s.
class PeopleBar extends StatelessWidget {
  final PersonTappedCallback onPersonTapped;
  final List<Person> people;
  final List<BoxShadow> boxShadow;
  final double borderWidth;
  final FractionalOffset alignment;

  const PeopleBar(
      {this.people,
      this.onPersonTapped,
      this.boxShadow: null,
      this.borderWidth: null,
      this.alignment: const FractionalOffset(1.0, 0.5)});

  @override
  Widget build(BuildContext context) => (people != null && people.isNotEmpty)
      ? new Container(
          margin: const EdgeInsets.only(top: 16.0),
          height: personDiameter,
          child: new Align(
              alignment: alignment,
              child: new Block(
                  children: _createPeopleWidgets(),
                  scrollDirection: Axis.horizontal)))
      : new Container(width: 0.0, height: 0.0);

  List<Widget> _createPeopleWidgets() {
    final List<Widget> relevantPeopleComponents = <Widget>[];
    for (int i = 0; i < people.length; i++) {
      relevantPeopleComponents.add(new PersonComponent(people[i],
          applyPadding: true,
          first: i == 0,
          last: i == (people.length - 1),
          onPersonTapped: onPersonTapped,
          boxShadow: boxShadow,
          borderWidth: borderWidth));
    }
    return relevantPeopleComponents;
  }
}
