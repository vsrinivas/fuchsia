// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A simple app with two labels and two buttons.

import 'package:flutter/widgets.dart';

const Color blue = Color(0xFF003D75);
const Color yellow = Color(0xFFFDAB03);

void main() {
  runApp(
    Directionality(
      textDirection: TextDirection.ltr,
      child: SimpleButtonsAndLabelPage(),
    ),
  );
}

class TapCounter extends StatelessWidget {
  const TapCounter({
    @required this.label,
    this.width = 150,
    this.height = 50,
    Key key,
  })  : assert(label != null),
        assert(width != null),
        assert(height != null),
        assert(width >= 0),
        assert(height >= 0),
        super(key: key);

  final String label;
  final double width;
  final double height;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      container: true, // gives this a better tap target.
      child: SizedBox(
        height: height,
        width: width,
        child: Center(
          child: Text(label),
        ),
      ),
    );
  }
}

class TapButton extends StatelessWidget {
  const TapButton({
    @required this.label,
    @required this.color,
    @required this.onTap,
    this.width = 150,
    this.height = 50,
    Key key,
  })  : assert(label != null),
        assert(color != null),
        assert(onTap != null),
        assert(width != null),
        assert(height != null),
        assert(width >= 0),
        assert(height >= 0),
        super(key: key);

  final String label;
  final Color color;
  final VoidCallback onTap;
  final double width;
  final double height;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      button: true,
      child: GestureDetector(
        onTap: onTap,
        child: Container(
          width: width,
          height: height,
          color: color,
          child: Center(child: Text(label)),
        ),
      ),
    );
  }
}

class SimpleButtonsAndLabelPage extends StatefulWidget {
  const SimpleButtonsAndLabelPage({Key key}) : super(key: key);

  @override
  State<SimpleButtonsAndLabelPage> createState() =>
      SimpleButtonsAndLabelPageState();
}

class SimpleButtonsAndLabelPageState extends State<SimpleButtonsAndLabelPage> {
  int blueCounter;
  int yellowCounter;

  @override
  void initState() {
    super.initState();
    blueCounter = 0;
    yellowCounter = 0;
  }

  @override
  Widget build(BuildContext context) {
    const double buttonHeight = 50;

    return SizedBox.expand(
      child: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.spaceEvenly,
          children: <Widget>[
            TapCounter(
                label:
                    'Blue tapped $blueCounter time${blueCounter != 1 ? 's' : ''}'),
            TapCounter(
                label:
                    'Yellow tapped $yellowCounter time${yellowCounter != 1 ? 's' : ''}'),
            // Embed the buttons in a ListView that is constrained to only be able to
            // display a single button at a time
            SizedBox(
              height: buttonHeight,
              child: ListView(
                children: <Widget>[
                  TapButton(
                    label: 'Blue',
                    color: blue,
                    onTap: () => setState(() {
                      blueCounter++;
                    }),
                    height: buttonHeight,
                  ),
                  TapButton(
                    label: 'Yellow',
                    color: yellow,
                    onTap: () => setState(() {
                      yellowCounter++;
                    }),
                    height: buttonHeight,
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
