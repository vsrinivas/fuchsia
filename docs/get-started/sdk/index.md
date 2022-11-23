# Get started with the Fuchsia SDK

This guide provides step-by-step instructions on setting up the Fuchsia SDK
development environment on your host machine using a terminal or
Visual Studio Code (VS Code). Then the guide walks through the basic workflows
of building, running, debugging, and testing Fuchsia components using the
[Fuchsia SDK][using-the-sdk].

Important: The Fuchsia SDK is in active development. At the moment, Fuchsia does
not support general public usage of the Fuchsia SDK. The APIs in the SDK are
subject to change without notice.

Which development environment are you using for this guide?:

<div class="device-selector-intro">
  <devsite-nav-buttons name="ide" type="text" param="always">
    <button value="none" default>Terminal</button>
    <button value="vscode">VS Code</button>
  </devsite-nav-buttons>
</div>

{% dynamic if request.query_string.ide == "none" %}

Complete the following sections:

1. [Prerequisites](#prerequisites)
1. [Clone the SDK samples repository](#clone-the-sdk-samples-repository)
1. [Start the emulator](#start-the-emulator)
1. [Build and run the sample component](#build-and-run-the-sample-component)
1. [View symbolized logs](#view-symbolized-logs)
1. [Debug the sample component](#debug-the-sample-component)
1. [Inspect components](#inspect-components)
1. [Run tests](#run-tests)

{% dynamic elif request.query_string.ide == "vscode" %}

Complete the following sections:

1. [Prerequisites](#prerequisites)
1. [Clone the SDK samples repository](#clone-the-sdk-samples-repository)
1. [Configure a VS Code workspace](#configure-a-vs-code-workspace)
1. [Start the emulator](#start-the-emulator)
1. [Build and run the sample component](#build-and-run-the-sample-component)
1. [View symbolized logs](#view-symbolized-logs)
1. [Debug the sample component](#debug-the-sample-component)
1. [Inspect components](#inspect-components)
1. [Run tests](#run-tests)

{% dynamic endif %}

Found an issue? Please [let us know][sdk-bug]{:.external}.

## Prerequisites {:#prerequisites .numbered}

This guide requires that your host machine meets the following criteria:

- An x64-based machine running Linux or macOS.
- Has at least 15 GB of storage space.
- Supports virtualization for running a [QEMU]{:.external}-based emulator.
- IPv6 is enabled.
- [Git][git-install]{:.external} is installed.
{% dynamic if request.query_string.ide == "vscode" %}
- [Visual Studio Code][vscode-install]{:.external} is installed.
{% dynamic endif %}

## Clone the SDK samples repository {:#clone-the-sdk-samples-repository .numbered}

{% dynamic if request.query_string.ide == "none" %}
<<_common/_get-started-sdk-clone-sdk-repo-terminal.md>>
{% dynamic elif request.query_string.ide == "vscode" %}
<<_common/_get-started-sdk-clone-sdk-repo-vs-code.md>>
{% dynamic endif %}

{% dynamic if request.query_string.ide == "vscode" %}
## Configure a VS Code workspace {:#configure-a-vs-code-workspace .numbered}

<<_common/_get-started-sdk-configure-vs-code.md>>
{% dynamic endif %}

## Start the emulator {:#start-the-emulator .numbered}

{% dynamic if request.query_string.ide == "none" %}
<<_common/_get-started-sdk-start-emulator-terminal.md>>
{% dynamic elif request.query_string.ide == "vscode" %}
<<_common/_get-started-sdk-start-emulator-vs-code.md>>
{% dynamic endif %}

## Build and run the sample component {:#build-and-run-the-sample-component .numbered}

{% dynamic if request.query_string.ide == "none" %}
<<_common/_get-started-sdk-build-and-run-terminal.md>>
{% dynamic elif request.query_string.ide == "vscode" %}
<<_common/_get-started-sdk-build-and-run-vs-code.md>>
{% dynamic endif %}

## View symbolized logs {:#view-symbolized-logs .numbered}

{% dynamic if request.query_string.ide == "none" %}
<<_common/_get-started-sdk-view-symbolized-logs-terminal.md>>
{% dynamic elif request.query_string.ide == "vscode" %}
<<_common/_get-started-sdk-view-symbolized-logs-vs-code.md>>
{% dynamic endif %}

## Debug the sample component {:#debug-the-sample-component .numbered}

{% dynamic if request.query_string.ide == "none" %}
<<_common/_get-started-sdk-debug-component-terminal.md>>
{% dynamic elif request.query_string.ide == "vscode" %}
<<_common/_get-started-sdk-debug-component-vs-code.md>>
{% dynamic endif %}

## Inspect components {:#inspect-components .numbered}

Retrieve a component's data exposed by Fuchsia's Inspect API. This data can be
any set of specialized information that a Fuchsia component is programmed to
collect while it is running on the device.

Note: For a component to collect and expose inspect data, the implementation of
inspect operations and data types must be placed in the component’s code.
Developers use this inspect feature to collect and expose information that will
be helpful for debugging the component or the system. For details, see
[Fuchsia component inspection overview][inspect-overview].

The tasks include:

- Scan the list of components on the device that expose inspect data (for
  example, the `bootstrap/archivist` component).
- Scan the list of selectors provided by the `bootstrap/archivist` component.
- Inspect a specific set of data from the `bootstrap/archivist` component.

Do the following:

1. View the list of components on the device that expose inspect data:

   ```posix-terminal
   tools/ffx inspect list
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx inspect list
   <component_manager>
   bootstrap/archivist
   bootstrap/driver_manager
   bootstrap/fshost
   bootstrap/fshost/blobfs
   bootstrap/fshost/fxfs
   ...
   core/ui/scenic
   core/vulkan_loader
   core/wlancfg
   core/wlandevicemonitor
   core/wlanstack
   ```

   Notice that the `bootstrap/archivist` component is on the list.

1. View all available selectors for the `bootstrap/archivist` component:

   ```posix-terminal
   tools/ffx inspect selectors bootstrap/archivist
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx inspect selectors bootstrap/archivist
   bootstrap/archivist:root/archive_accessor_stats/all/inspect/batch_iterator/get_next:errors
   bootstrap/archivist:root/archive_accessor_stats/all/inspect/batch_iterator/get_next:requests
   bootstrap/archivist:root/archive_accessor_stats/all/inspect/batch_iterator/get_next:responses
   ...
   ```

   Each of these selectors represents a different type of data you can inspect.

1. Inspect the `bootstrap/archivist` component for the recent events data:

   ```posix-terminal
   tools/ffx inspect show bootstrap/archivist:root/events/recent_events
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx inspect show bootstrap/archivist:root/events/recent_events
   bootstrap/archivist:
     metadata:
       filename = fuchsia.inspect.Tree
       component_url = fuchsia-boot:///#meta/archivist.cm
       timestamp = 705335717538
     payload:
       root:
         events:
           recent_events:
             361:
               @time = 6272744049
               event = component_stopped
               moniker = core/trace_manager/cpuperf_provider
             362:
               @time = 6283370267
               event = log_sink_requested
               moniker = core/session-manager
             ...
             556:
               @time = 415796882099
               event = log_sink_requested
               moniker = core/debug_agent
             557:
               @time = 453898419738
               event = component_started
               moniker = core/ffx-laboratory:hello_world
             558:
               @time = 453899964568
               event = log_sink_requested
               moniker = core/ffx-laboratory:hello_world
             559:
               @time = 453900332656
               event = log_sink_requested
               moniker = core/ffx-laboratory:hello_world
             560:
               @time = 495458923475
               event = component_stopped
               moniker = core/ffx-laboratory:hello_world
   ```

   This data records all the events triggered by components on the device so
   far.

## Run tests {:#run-tests .numbered}

Run tests on the device by launching test components, which are included in the
[SDK samples repository][sdk-samples-repo]{:.external}.

The tasks include:

- Build and run the sample test components.
- Update one of the tests to fail.
- Verify the failure in the test results.

Do the following:

1. Build and run the sample test components:

   ```posix-terminal
   tools/bazel test --config=fuchsia_x64 --test_output=all //src/hello_world:test_pkg
   ```

   This command runs all the tests in the Hello World component’s test package
   ([`hello_world:test_pkg`][hello-world-test-package]{:.external}).

   The command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/bazel test --config=fuchsia_x64 --test_output=all //src/hello_world:test_pkg
   INFO: Analyzed target //src/hello_world:test_pkg (10 packages loaded, 577 targets configured).
   INFO: Found 1 test target...
   INFO: From Testing //src/hello_world:test_pkg:
   ==================== Test output for //src/hello_world:test_pkg:
   Running 2 test components...
   added repository bazel.test.pkg.hello.gtest
   Running test 'fuchsia-pkg://bazel.test.pkg.hello.gtest/hello_test#meta/hello_gtest_autogen_cml.cm'
   [RUNNING]    HelloTest.BasicAssertions
   [stdout - HelloTest.BasicAssertions]
   Running main() from gmock_main.cc
   Example stdout.
   [PASSED]    HelloTest.BasicAssertions

   1 out of 1 tests passed...
   fuchsia-pkg://bazel.test.pkg.hello.gtest/hello_test#meta/hello_gtest_autogen_cml.cm completed with result: PASSED
   added repository bazel.test.pkg.hello.test
   Running test 'fuchsia-pkg://bazel.test.pkg.hello.test/hello_test#meta/hello_test_autogen_cml.cm'
   [RUNNING]    main
   [stdout - main]
   Example stdout.
   [PASSED]    main

   1 out of 1 tests passed...
   fuchsia-pkg://bazel.test.pkg.hello.test/hello_test#meta/hello_test_autogen_cml.cm completed with result: PASSED
   2 test components passed.
   ================================================================================
   Target //src/hello_world:test_pkg up-to-date:
     bazel-bin/src/hello_world/test_pkg_test_package.sh
   INFO: Elapsed time: 19.563s, Critical Path: 11.83s
   INFO: 105 processes: 46 internal, 56 linux-sandbox, 3 local.
   INFO: Build completed successfully, 105 total actions
   //src/hello_world:test_pkg                                               PASSED in 3.8s

   Executed 1 out of 1 test: 1 test passes.
   INFO: Build completed successfully, 105 total actions
   ```

1. Use a text editor to edit the `src/hello_world/hello_gtest.cc` file, for
   example:

   ```posix-terminal
   nano src/hello_world/hello_gtest.cc
   ```

1. Replace `EXPECT_STRNE()` with `EXPECT_STREQ()`:

   The test should look like below:

   ```none {:.devsite-disable-click-to-copy}
   TEST(HelloTest, BasicAssertions) {
     std::cout << "Example stdout." << std::endl;

     // Expect two strings not to be equal.
     {{ '<strong>' }}EXPECT_STREQ("hello", "world");{{ '</strong>' }}
     // Expect equality.
     EXPECT_EQ(7 * 6, 42);
   }
   ```

   This change will cause the [GoogleTest][google-test]{:.external}
   (`hello_gtest`) to fail.

1. Save the file and exit the text editor.

1. To verify that the updated test now fails, build and run the `hello_gtest`
   component:

   ```posix-terminal
   tools/bazel test --config=fuchsia_x64 --test_output=all //src/hello_world:test_pkg.hello_gtest
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/bazel test --config=fuchsia_x64 --test_output=all //src/hello_world:test_pkg.hello_gtest
   INFO: Analyzed target //src/hello_world:test_pkg.hello_gtest (0 packages loaded, 0 targets configured).
   INFO: Found 1 test target...
   FAIL: //src/hello_world:test_pkg.hello_gtest (see /home/alice/.cache/bazel/_bazel_alice/ea119f1048230a864836be3d62fead2c/execroot/__main__/bazel-out/x86_64-fastbuild/testlogs/src/hello_world/test_pkg.hello_gtest/test.log)
   INFO: From Testing //src/hello_world:test_pkg.hello_gtest:
   ==================== Test output for //src/hello_world:test_pkg.hello_gtest:
   added repository bazel.test.pkg.hello.gtest
   Running test 'fuchsia-pkg://bazel.test.pkg.hello.gtest/hello_test#meta/hello_gtest_autogen_cml.cm'
   [RUNNING]   HelloTest.BasicAssertions
   [stdout - HelloTest.BasicAssertions]
   Running main() from gmock_main.cc
   Example stdout.
   src/hello_world/hello_gtest.cc:14: Failure
   Expected equality of these values:
     "hello"
     "world"
   [FAILED]   HelloTest.BasicAssertions

   Failed tests: HelloTest.BasicAssertions
   0 out of 1 tests passed...
   fuchsia-pkg://bazel.test.pkg.hello.gtest/hello_test#meta/hello_gtest_autogen_cml.cm completed with result: FAILED
   One or more test runs failed.
   Tests failed.
   ================================================================================
   Target //src/hello_world:test_pkg.hello_gtest up-to-date:
     bazel-bin/src/hello_world/test_pkg.hello_gtest_run_component.sh
   INFO: Elapsed time: 4.195s, Critical Path: 3.79s
   INFO: 11 processes: 2 internal, 6 linux-sandbox, 3 local.
   INFO: Build completed, 1 test FAILED, 11 total actions
   //src/hello_world:test_pkg.hello_gtest                                   FAILED in 1.8s
     /home/alice/.cache/bazel/_bazel_alice/ea119f1048230a864836be3d62fead2c/execroot/__main__/bazel-out/x86_64-fastbuild/testlogs/src/hello_world/test_pkg.hello_gtest/test.log

   INFO: Build completed, 1 test FAILED, 11 total actions
   ```

**Congratulations! You're now all set with the Fuchsia SDK!**

## Next steps {:#next-steps}

Learn more about the Fuchsia platform and tools in
[Fuchsia SDK Fundamentals][fundamentals].

## Appendices

### Clean up the environment {:#clean-up-the-environment}

If you run into a problem while following this guide and decide to start over
from the beginning, consider running the commands below to clean up your
development environment (that is, to clean up directories, build artifacts,
downloaded files, symlinks, configuration settings, and more).

Remove the package repositories created in this guide:

```posix-terminal
tools/ffx repository remove workstation-packages
```

```posix-terminal
tools/ffx repository server stop
```

Remove all existing configurations and data of `ffx`:

- {Linux}

  ```posix-terminal
  tools/ffx daemon stop
  ```

  ```posix-terminal
  rm -rf $HOME/.local/share/Fuchsia/ffx
  ```

- {macOS}

  ```posix-terminal
  tools/ffx daemon stop
  ```

  ```posix-terminal
  rm -rf $HOME/Library/Caches/Fuchsia/ffx
  ```

  ```posix-terminal
  rm -fr $HOME/Library/Fuchsia/ffx
  ```

When Bazel fails to build, try the commands below:

- {Linux}

  Note: Running `bazel clean` or deleting the `$HOME/.cache/bazel` directory
  deletes artifacts downloaded by Bazel, which can be around 4 GB. This means
  Bazel will need to download dependencies again next time you run
  `bazel build`.

  ```posix-terminal
  tools/bazel clean --expunge
  ```

  ```posix-terminal
  tools/bazel shutdown && rm -rf $HOME/.cache/bazel
  ```

- {macOS}

  Note: Running `bazel clean` or deleting the `/private/var/temp/bazel$USER`
  directory deletes artifacts downloaded by Bazel, which can be around 4 GB.
  This means Bazel will need to download dependencies again next time you run
  `bazel build`.

  ```posix-terminal
  tools/bazel clean --expunge
  ```

  ```posix-terminal
  tools/bazel shutdown && rm -rf /private/var/tmp/bazel$USER
  ```

Remove the `fuchsia-getting-started` directory and its artifacts:

Caution: If the SDK samples repository is cloned to a different location than
`$HOME/fuchsia-getting-started`, adjust the directory in the command below. Be
extremely careful with the directory path when you run the `rm -rf <DIR>`
command.

```posix-terminal
rm -rf $HOME/fuchsia-getting-started
```

Other clean up commands:

```posix-terminal
killall ffx
```

```posix-terminal
killall pm
```

### Update the firewall rules {:#update-the-firewall-rules}

When you launch the sample component (for instance, using the command
`tools/bazel run`), you might run into an issue where the command hangs for a
long time and eventually fails with the following error:

```none {:.devsite-disable-click-to-copy}
Lifecycle protocol could not start the component instance: InstanceCannotResolve
```

In that case, you may need to update the firewall rules on your host machine.

If you’re using the `ufw` firewall, run the following commands:

```posix
sudo ufw allow proto tcp from fe80::/10 to any port 8083 comment 'Fuchsia Package Server'
```

```posix
sudo ufw allow proto tcp from fc00::/7 to any port 8083 comment 'Fuchsia Package Server'
```

However, for other non-`ufw`-based firewalls, you will need to ensure that port
8083 is available for the Fuchsia package server.

### Check if your Linux machine supports KVM virtualization {:#check-if-your-linux-machine-supports-kvm-virtualization}

To check if your Linux machine supports KVM hardware virtualization,
run the following command:

```posix-terminal
lscpu
```

This command prints output similar to the following:

```none {:.devsite-disable-click-to-copy}
$ lscpu
Architecture:            x86_64
  CPU op-mode(s):        32-bit, 64-bit
  Address sizes:         46 bits physical, 48 bits virtual
  Byte Order:            Little Endian
...
Virtualization features:
  {{ '<strong>' }}Virtualization:        VT-x{{ '</strong>' }}
  Hypervisor vendor:     KVM
  Virtualization type:   full
...
```

If you see the following field in the output, your Linux machine
**supports** KVM hardware virtualization:

```none {:.devsite-disable-click-to-copy}
  Virtualization:        VT-x
```

Note: If your Linux machine supports KVM hardware virtualization, see
[Set up KVM virtualization on a Linux machine](#set-up-kvm-virtualization-on-a-linux-machine)
to verify that KVM is configured correctly.

However, you may see that the `Virtualization` field is  missing in your output
(while the `Hypervisor vendor` and `Virtualization type` fields are still shown),
for example:

```none {:.devsite-disable-click-to-copy}
$ lscpu
...
Virtualization features:
  Hypervisor vendor:     KVM
  Virtualization type:   full
...
```

If your output does not show the `Virtualization` field, your Linux machine
**does not support** KVM hardware virtualization.

### Set up KVM virtualization on a Linux machine {:#set-up-kvm-virtualization-on-a-linux-machine}

Note: The instructions in this section require that
[your Linux machine supports KVM hardware virtualization](#check-if-your-linux-machine-supports-kvm-virtualization).

To verify that KVM is configured correctly on your Linux machine,
run the following `bash` shell script:

```posix-terminal
if [[ -w /dev/kvm ]] && grep '^flags' /proc/cpuinfo | grep -qE 'vmx|svm'; then echo 'KVM is working'; else echo 'KVM not working'; fi
```

Verify that this shell script prints the following output:

```none {:.devsite-disable-click-to-copy}
KVM is working
```

If the output is `KVM is working`, KVM hardware virtualization is
enabled on your Linux machine.

However, if the output is `KVM not working`, do the following to
enable KVM hardware virtualization:

1. Add yourself to the `kvm` group on your Linux machine:

   ```posix-terminal
   sudo usermod -a -G kvm ${USER}
   ```

1. Reboot the machine.
1. Run the `bash` shell script above again.

   Verify that the output now prints `KVM is working`.

<!-- Reference links -->

[clang]: https://clang.llvm.org/
[femu]: /docs/development/sdk/ffx/start-the-fuchsia-emulator.md
[ffx]: https://fuchsia.dev/reference/tools/sdk/ffx
[fuchsia-component]: /docs/concepts/components/v2/README.md
[fuchsia-debugger]: /docs/development/sdk/ffx/start-the-fuchsia-debugger.md
[fuchsia-idk]: /docs/development/idk/README.md
[fuchsia-package-server]: /docs/development/sdk/ffx/create-a-package-repository.md
[fundamentals]: /docs/get-started/sdk/learn/README.md
[git-install]: https://git-scm.com/book/en/v2/Getting-Started-Installing-Git
[google-test]: https://google.github.io/googletest/
[hello-world-component]: https://fuchsia.googlesource.com/sdk-samples/getting-started/+/refs/heads/main/src/hello_world/
[hello-world-test-package]: https://fuchsia.googlesource.com/sdk-samples/getting-started/+/refs/heads/main/src/hello_world/BUILD.bazel#68
[inspect-overview]: /docs/development/diagnostics/inspect/README.md
[kvm]: https://www.linux-kvm.org/page/Main_Page
[qemu]: https://www.qemu.org/
[sdk-bug]: https://bugs.fuchsia.dev/p/fuchsia/issues/entry?template=Bazel
[sdk-samples-repo]: https://fuchsia.googlesource.com/sdk-samples/getting-started
[symbolize-logs]: /docs/development/sdk/ffx/symbolize-logs.md
[ticket-01]: https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=97909
[ticket-94614]: https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=94614
[using-the-sdk]: /docs/development/sdk/index.md
[zxdb-user-guide]: /docs/development/debugger/README.md
[vscode-install]: https://code.visualstudio.com/Download
