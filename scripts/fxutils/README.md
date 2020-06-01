# An fx utilities library for Fuchsia developers.

## Usage

A simple usage example:

```dart
import 'package:fxutils/fxutils.dart';

main() async {
  final fx = Fx();
  final rawStatusOutput = await fx.getSubCommandOutput('status');
}
```