# Dart SDK

This frontend produces an [SDK source][sdk-source] for Dart that integrates with
the [Pub package manager][pub].

## Testing

Generate the SDK
```
$ scripts/sdk/dart-pub/generate.py --manifest path/to/manifest --output my_shiny_sdk
```

Try to set up a few packages
```
$ cd my_shiny_sdk/packages/some_package
$ pub get
```


[sdk-source]: https://www.dartlang.org/tools/pub/dependencies#sdk
[pub]: https://www.dartlang.org/tools/pub
