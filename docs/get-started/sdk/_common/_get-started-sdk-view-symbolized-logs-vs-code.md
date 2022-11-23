Examine the [symbolized logs][symbolize-logs] (that is, human readable stack
traces) of a crashed component.

The tasks include:

- Update the sample component to crash when it's started.
- Build and run the sample component, which generates and registers the debug
  symbols of the component.
- Verify that the crashed component's logs are in symbolized format.

In VS Code, do the following:

1. Click the Explorer icon on the left side of the VS Code window.
1. Open the `src/hello_world/hello_world.cc` file.
1. Just above the line `return 0;`, add the following line:

   ```
   abort();
   ```

   The `main()` method should look like below:

   ```none {:.devsite-disable-click-to-copy}
   int main() {
     std::cout << "Hello again, World!\n";
     {{ '<strong>' }}abort();{{ '</strong>' }}
     return 0;
   }
   ```

   This update will cause the component to crash immediately after printing a
   message.

1. To save the file, press `CTRL+S` (or `CMD+S` on macOS).

1. Click the **TERMINAL** tab on the VS Code window.

1. In the terminal, build and run the sample component:

   ```posix-terminal
   tools/bazel run --config=fuchsia_x64 //src/hello_world:pkg.component
   ```

   Building a component automatically generates and registers the component’s
   debug symbols in your development environment.

1. For newly registered symbols to be used in your environment, restart the
   `ffx` daemon:

   Note: This is a temporary workaround. This issue is being tracked in
   [Issue 94614][ticket-94614]{:.external}.

   ```posix-terminal
   tools/ffx daemon stop
   ```

   A new instance of the `ffx `daemon starts the next time you run a `ffx`
   command or view device logs in VS Code.

1. Click the **FUCHSIA LOGS** tab on the VS Code window.

1. In the **Filter logs...** text box, type `moniker:klog` and press
   **Enter**.

1. Verify that the sample component's crash stack is symbolized in the kernel
   logs:

   <img class="vscode-image vscode-image-center"
   alt="This figure shows the kernel logs of the Fuchsia emulator device."
   src="images/get-started-vscode-symbolized-logs.png"/>

   Verify that the lines in the kernel logs show the exact filenames and line
   numbers (for example, `main() src/hello_world/hello_world.cc:9`) that
   might've caused the component to crash.
