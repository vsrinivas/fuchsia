# IDEs


A prebuilt Dart SDK is available for IDE consumption at:
`//third_party/dart/tools/sdks/<linux|mac>/dart-sdk`.
Note that this SDK is sometimes a few days behind the version of
`//third_party/dart`. If you require an up-to-date SDK, one gets built with
Fuchsia at:
`//out/<build-type>/dart_host/dart-sdk`.

## Troubleshooting

When you find the Dart analysis is not working properly in your IDE, try the
following:
- Delete `//out` and rebuild. Specifically, a release build overrides a debug
  build. This means that if you have a broken release build, any release build
  overrides a debug build. With a broken release build, no amount of correct
  rebuilding on debug will solve the issue until you delete
  `//out/release-x86-64`.
- Delete the .packages file in your project and rebuild.
