Build and run the [C++ Hello World component][hello-world-component]{:.external}
included in the SDK samples repository. [Components][fuchsia-component] are the
basic unit of executable software on Fuchsia.

The tasks include:

- Build and run the sample Hello World component.
- Make a change to the component.
- Repeat the build and run steps.
- Verify the change.

Do the following:

1. Build and run the sample component:

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

1. Verify the `Hello, World!` message in the device logs:

   ```posix-terminal
   tools/ffx log --filter hello_world dump
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx log --filter hello_world dump
   [2022-10-27 17:54:26.322][<ffx>]: logger started.
   [137.639][pkg-resolver][pkg-resolver][I] updated local TUF metadata for "fuchsia-pkg://bazel.pkg.component" to version RepoVersions { root: 1, timestamp: Some(1666893385), snapshot: Some(1666893385), targets: Some(1666893385) } while getting merkle for TargetPath("hello_world/0")
   [137.732][pkg-resolver][pkg-resolver][I] resolved fuchsia-pkg://bazel.pkg.component/hello_world as fuchsia-pkg://bazel.pkg.component/hello_world to e1a21b1f409cb31004e4ed995cebe094a0483056d305d2925b71080ffcfc88d7 with TUF
   {{ '<strong>' }}[137.761][ffx-laboratory:hello_world][I] Hello, World!{{ '</strong>' }}
   ```

1. Use a text editor to edit the `src/hello_world/hello_world.cc` file, for
   example:

   ```posix-terminal
   nano src/hello_world/hello_world.cc
   ```

1. Change the message to `"Hello again, World!"`.

   The `main()` method should look like below:

   ```none {:.devsite-disable-click-to-copy}
   int main() {
     {{ '<strong>' }}std::cout << "Hello again, World!\n";{{ '</strong>' }}
     return 0;
   }
   ```

1. Save the file and exit the text editor.

1. Build and run the sample component again:

   ```posix-terminal
   tools/bazel run --config=fuchsia_x64 //src/hello_world:pkg.component
   ```

1. Verify the `Hello again, World!` message in the device logs:

   ```posix-terminal
   tools/ffx log --filter hello_world dump
   ```

   This command prints output similar to the following;

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx log --filter hello_world dump
   ...
   [4885.928][pkg-resolver][pkg-resolver][I] resolved fuchsia-pkg://bazel.pkg.component/hello_world as fuchsia-pkg://bazel.pkg.component/hello_world to 916763545fe9df0299bf049359f6b09b8d9dac2881a6d6c016d929d970738586 with TUF
   {{ '<strong>' }}[4885.959][ffx-laboratory:hello_world][I] Hello again, World!{{ '</strong>' }}
   ```
