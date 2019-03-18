# IDEs

### Dart SDK

A prebuilt Dart SDK is available for IDE consumption at:
`//third_party/dart/tools/sdks/<linux|mac>/dart-sdk`. Note that this SDK is
sometimes a few days behind the version of `//third_party/dart`. If you require
an up-to-date SDK, one gets built with Fuchsia at:
`//out/<build-type>/dart_host/dart-sdk`.

## Visual Studio Code

1.  Download and install [Visual Studio Code](https://code.visualstudio.com/)
1.  [optional] Setup VS Code to launch from the command line

    *   For Macs: To allow running VS Code from the terminal using the `code`
        command, follow the instructions
        [here](https://code.visualstudio.com/docs/setup/mac#_launching-from-the-command-line)

    *   For Linux and Windows: This should already be done as part of the
        installation

1.  Install the following extensions:

    *   [Dart Code](https://marketplace.visualstudio.com/items?itemName=Dart-Code.dart-code):
        Support for programming in Dart
    *   [FIDL language support](https://marketplace.visualstudio.com/items?itemName=fuchsia-authors.language-fidl):
        Syntax highlighting support for Fuchsia's FIDL files
    *   [GN](https://marketplace.visualstudio.com/items?itemName=npclaudiu.vscode-gn):
        Syntax highlighting for GN build files
    *   Optional but helpful git extensions:
        *   [Git Blame](https://marketplace.visualstudio.com/items?itemName=waderyan.gitblame):
            See git blam information in the status bar
        *   [Git History](https://marketplace.visualstudio.com/items?itemName=donjayamanne.githistory):
            View git log, file history, etc.

1.  Here are some helpful user settings for Dart, you can open your user
    settings by typing `Ctrl+,`:

```json
{
  // Auto-formats your files when you save
  "editor.formatOnSave": true,

  // Don't run pub with fuchsia.
  "dart.runPubGetOnPubspecChanges": false,

  // Settings only when working in Dart
  "[dart]": {
    // Adds a ruler at 80 characters
    "editor.rulers": [
      80
    ],

    // Makes the tab size 2 spaces
    "editor.tabSize": 2,
  },
}

```

## Troubleshooting

If you find that the IDE is unable to find imports (red squigglies) that are
already correctly in your BUILD.gn dependencies, this is usually a sign that
Dart analysis is not working properly in your IDE.

When this happens, try the following:

### Rebuild

Delete `//out` and rebuild. Specifically, a release build overrides a debug
build. This means that if you have a broken release build, any release build
overrides a debug build. With a broken release build, no amount of correct
rebuilding on debug will solve the issue until you delete `//out/release-x64`.

### Remove pub output

1.  Delete the `.packages` and `pubspec.lock` files in your project (if
    present).
1.  Ensure that `"dart.runPubGetOnPubspecChanges": false,` is present in your
    VSCode preferences to prevent the files from reappearing whenever a
    `pubspec.yaml` file is edited.
1.  Reload VSCode to restart the Dart analyzer.
    1.  Press Ctrl+Shift+P to open the VSCode Command Palette
    1.  Select "Reload Window"
