import 'package:flutter/material.dart';
import 'package:tiler/tiler.dart' show TileModel;

/// Widget that acts as a button to remove currently focused mod's tile,
/// and drop target to remove a tile by drag-and-drop
class RemoveButtonTargetWidget extends StatefulWidget {
  const RemoveButtonTargetWidget({
    @required this.onTap,
  });

  /// Callback when button is tapped
  final VoidCallback onTap;

  @override
  _RemoveButtonTargetWidgetState createState() =>
      _RemoveButtonTargetWidgetState();
}

class _RemoveButtonTargetWidgetState extends State<RemoveButtonTargetWidget> {
  final _touching = ValueNotifier<bool>(false);

  @override
  Widget build(BuildContext context) {
    return DragTarget<TileModel>(
      builder: (_, candidateData, ___) {
        return GestureDetector(
          onTap: widget.onTap,
          onTapDown: (_) {
            _touching.value = true;
          },
          onTapCancel: () {
            _touching.value = false;
          },
          onTapUp: (_) {
            _touching.value = false;
          },
          child: AnimatedBuilder(
            animation: _touching,
            builder: (context, snapshot) {
              final hovering = candidateData.isNotEmpty || _touching.value;
              final foreground = hovering ? Colors.white : Colors.black;
              final background = hovering ? Colors.black : Colors.white;
              return Material(
                color: background,
                elevation: 24,
                child: AspectRatio(
                  aspectRatio: 1,
                  child: Container(
                    decoration: BoxDecoration(
                      border: Border.all(
                        width: 2.0,
                        color: foreground,
                      ),
                    ),
                    child: FractionallySizedBox(
                      widthFactor: .75,
                      child: Center(
                        child: Container(
                          height: 2.0,
                          color: foreground,
                        ),
                      ),
                    ),
                  ),
                ),
              );
            },
          ),
        );
      },
    );
  }
}
