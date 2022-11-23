Build and run the [C++ Hello World component][hello-world-component]{:.external}
included in the SDK samples repository. [Components][fuchsia-component] are the
basic unit of executable software on Fuchsia.

The tasks include:

- Build and run the sample Hello World component.
- Make a change to the component.
- Repeat the build and run steps.
- Verify the change.

In VS Code, do the following:

1. Click the **TERMINAL** tab on the VS Code window.

1. In the terminal, build and run the sample component:

   ```posix-terminal
   tools/bazel run --config=fuchsia_x64 //src/hello_world:pkg.component
   ```

   When the build is successful, this command generates build artifacts in a
   temporary Fuchsia package repository, which is then removed after the
   component runs.

   The command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/bazel run --config=fuchsia_x64 //src/hello_world:pkg.component
   INFO: Build options --copt, --cpu, --crosstool_top, and 1 more have changed, discarding analysis cache.
   INFO: Analyzed target //src/hello_world:pkg.component (20 packages loaded, 2449 targets configured).
   INFO: Found 1 target...
   Target //src/hello_world:pkg.component up-to-date:
     bazel-bin/src/hello_world/pkg.component_run_component.sh
   INFO: Elapsed time: 4.709s, Critical Path: 2.47s
   INFO: 129 processes: 104 internal, 24 linux-sandbox, 1 local.
   INFO: Build completed successfully, 129 total actions
   INFO: Build completed successfully, 129 total actions
   added repository bazel.pkg.component
   URL: fuchsia-pkg://bazel.pkg.component/hello_world#meta/hello_world.cm
   Moniker: /core/ffx-laboratory:hello_world.cm
   Creating component instance...
   Starting component instance...
   Success! The component instance has been started.
   ```

1. Check the status of the `hello_world` component:

   ```posix-terminal
   tools/ffx component show hello_world
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx component show hello_world
                  Moniker:  /core/ffx-laboratory:hello_world.cm
                      URL:  fuchsia-pkg://bazel.pkg.component/hello_world#meta/hello_world.cm
              Instance ID:  None
                     Type:  CML Component
          Component State:  Resolved
    Incoming Capabilities:  /svc/fuchsia.logger.LogSink
     Exposed Capabilities:
              Merkle root:  ec7f699b421f74843fbc8a24491a347790ece29c513b7b128b84a3e36e7311d7
          Execution State:  Stopped
   ```

   The output shows that the `hello_world` component has run and is now
   terminated (`Stopped`).

1. Click the target device (`fuchsia-emulator`) icon
   at the bottom of the VS Code window.

   This opens the Command Palette at the top of the VS Code window.

1. Click **Show log for fuchsia-emulator** in the Command Palette.

   This opens the FUCHSIA LOGS tab and streams the device logs of
   your current Fuchsia target.

   <img class="vscode-image vscode-image-center"
   alt="This figure shows the how to connect the Fuchsia VS Code extension
   to a Fuchsia device."
   src="/docs/reference/tools/editors/vscode/images/extensions/ext-view-logs.png"/>

   Note: It may take a few minutes to load all the logs cached on the host
   machine. To stop the streaming of logs, click the
   <span class="material-icons">pause</span> icon at the top right corner of
   the FUCHSIA LOGS panel.

1. To fit the messages on the panel, click the "Wrap logs" icon
   at the top right corner of the FUCHSIA LOGS panel.

1. In the **Filter logs** text box, type `hello_world` and
   press **Enter**.

   <img class="vscode-image vscode-image-center"
   alt="This figure shows the 'Hello, World!' message in the Fuchsia logs."
   src="images/get-started-vscode-hello-world-log.png"/>

   Notice that `Hello, World!` is printed from the `hello_world` component.

   Note: For more information on filtering syntax, see
   [Filter Fuchsia logs][filter-vscode-logs].

1. Click the Explorer icon on the left side of the VS Code.

1. Open the `src/hello_world/hello_world.cc` file.

1. Change the message to `"Hello again, World!"`.

   The `main()` method should look like below:

   ```none {:.devsite-disable-click-to-copy}
   int main() {
     {{ '<strong>' }}std::cout << "Hello again, World!\n";{{ '</strong>' }}
     return 0;
   }
   ```

1. To save the file, press `CTRL+S` (or `CMD+S` on macOS).

1. Click the **TERMINAL** tab on the VS Code window.

1. In the terminal, build and run the `hello_world` component again:

   ```posix-terminal
   tools/bazel run --config=fuchsia_x64 //src/hello_world:pkg.component
   ```

1. Click the **FUCHSIA LOGS** tab on the VS Code window.

1. Verify that `Hello again, World!` is printed in the logs:

   <img class="vscode-image vscode-image-center"
   alt="This figure shows the 'Hello again, World!' message in the Fuchsia logs."
   src="images/get-started-vscode-hello-again-world-log.png"/>

<!-- Reference links -->

[filter-vscode-logs]: /docs/reference/tools/editors/vscode/fuchsia-ext-using.md#filter_fuchsia_logs
