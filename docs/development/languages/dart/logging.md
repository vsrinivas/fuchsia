# Logging in Dart

Dart programs on Fuchsia generally write log messages with the `lib.logging` package, consuming and
initializing it through the `fuchsia_logger` package.

See the [language agnostic logging docs](/docs/development/logs/concepts.md) for more information
about recording and viewing logs.

## Requirements

### GN dependency

The necessary packages can be included with an addtion to `deps` in `BUILD.gn`:

```
deps = [
  "//topaz/public/dart/fuchsia_logger",
]
```

The `fuchsia_logger` package also provides Dart's `lib.logging`.

See [Dart: Overview][dart-dev] for more information about building Dart within Fuchsia.

### Sandbox dependency

In order to connect to a diagnostics service, `fuchsia.logger.LogSink` must be in the sandbox
of the connecting component's [`.cmx` file]:

```
{
  "sandbox": {
    "services": [
      "fuchsia.logger.LogSink"
    ]
  }
}
```

The syslog library will fallback to `stderr` if the `LogSink` connection fails.

## Initialization

In your main function, call the `setupLogger()` function to initialize logging:

```dart
import 'package:fuchsia_logger/logger.dart';

main() {
  // process name will be used if no name is provided here
  setupLogger(name: 'my-component');
}
```

### Configure severity

By default only messages with `INFO` severity or higher are printed. Severity level can be adjusted
by providing the `level` parameter in the `setupLogger()` call.

For example, to make all log messages appear in [`fx log`]:

```dart
setupLogger(name: 'noisy-component', level: Level.ALL);
```

## Recording messages

The `log` object is a [Logger] instance.

```dart
import 'package:fuchsia_logger/logger.dart';

log.finest('quietest');      // maps to TRACE
log.finer('also quietest');  // maps to TRACE also
log.fine('quiet');           // maps to DEBUG
log.info('hello world!');    // maps to INFO
log.warning('uhhh');         // maps to WARN
log.severe('oh no!');        // maps to ERROR
log.shout('this is fatal.'); // maps to FATAL
```

## Standard streams

`print` goes to standard out (`stdout`).

See [`stdout` & `stderr`] in the language-agnostic logging docs for details on the routing of stdio
streams in the system.

[Logger]: https://pub.dev/documentation/logging/latest/logging/Logger-class.html
[`fx log`]: /docs/development/logs/viewing.md
[dart-dev]: /docs/development/languages/dart/README.md
[`.cmx` file]: /docs/concepts/components/v1/component_manifests.md
[`stdout` & `stderr`]: /docs/development/logs/recording.md#stdout-stderr
