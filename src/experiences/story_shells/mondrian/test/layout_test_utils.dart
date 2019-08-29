import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mondrian/models/surface/positioned_surface.dart';

/// Utility function to assert properties of a [PositionedSurface].
void assertSurfaceProperties(PositionedSurface surface,
    {double height, double width, Offset topLeft, Offset bottomRight}) {
  expect(surface.surface, isNotNull);
  Rect position = surface.position;
  // TODO(jphsiao): remove hacks to make tests pass while width and height
  // are incorrect. Issue is tracked in flutter as #18169
  if (height != null) {
    expect(_isWithinMargin(position.height, height, 0.01), true);
  }
  if (width != null) {
    expect(_isWithinMargin(position.width, width, 0.01), true);
  }
  if (topLeft != null) {
    double x = position.topLeft.dx;
    expect(_isWithinMargin(x, topLeft.dx, 0.01), true);

    double y = position.topLeft.dy;
    expect(_isWithinMargin(y, topLeft.dy, 0.01), true);
  }
  if (bottomRight != null) {
    double x = position.bottomRight.dx;
    expect(_isWithinMargin(x, bottomRight.dx, 0.01), true);

    double y = position.bottomRight.dy;
    expect(_isWithinMargin(y, bottomRight.dy, 0.01), true);
  }
}

/// Determines whether two values are within the provided margin
bool _isWithinMargin(double expected, double actual, double margin) {
  return (expected - actual).abs() < margin;
}
