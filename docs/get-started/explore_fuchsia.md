# Explore Fuchsia {#explore-fuchsia}

Once you have a device or emulator up an running, explore what you can do next
with Fuchsia.

## Run ffx commands {#run-ffx-commands}

[`ffx`][ffx-overview] is a host tool for Fuchsia target workflows that
provides the consistent development experience across all Fuchsia environments
and host platforms.

See the following example `ffx` commands:

*   Display the list of devices:

    ```posix-terminal
    ffx target list
    ```

*   Display the device information:

    ```posix-terminal
    ffx target show
    ```

*   Print the device logs:

    ```posix-terminal
    ffx target log watch
    ```

*   Reboot the device:

    ```posix-terminal
    ffx target reboot
    ```

## Run some examples {#run-examples}

Follow these guides to try out some example components:

*   [Run an example component](/docs/development/run/run-examples.md)
*   [Run a test component](/docs/development/run/run-test-component.md)

## Write software for Fuchsia {#write-software}

The basic executable units of software in Fuchsia are
[components](/docs/concepts/components/v2) that interact with each other using.
[FIDL protocols](/docs/concepts/fidl/overview.md). Explore the following guides
to learn more about building component-based software:

*   [Build components](/docs/development/components/build.md)
*   [Fuchsia Interface Definition Language](/docs/development/languages/fidl/README.md)
*   [FIDL tutorials](/docs/development/languages/fidl/tutorials/overview.md)

## Contribute changes {#contribute-changes}

To submit your contribution to Fuchsia, see
[Contribute changes][contribute-changes].

## See also

*   [fx workflows](/docs/development/build/fx.md)
*   [Workflow tips and questions](/docs/development/source_code/workflow_tips_and_faq.md)
*   [Configure editors](/docs/development/editors/)
*   [Source code layout](/docs/concepts/source_code/layout.md)
*   [Build system](/docs/concepts/build_system/index.md)

<!-- Reference links -->

[components]: /docs/concepts/components/v2
[run-examples]: /docs/development/run/run-examples.md
[ffx-overview]: /docs/development/tools/ffx/overview.md
[fidl]: /docs/development/languages/fidl
[fidl-tutorials]: /docs/development/languages/fidl/tutorials/overview.md
[fidl-concepts]: /docs/concepts/fidl/overview.md
[run-fuchsia-tests]: /docs/development/testing/run_fuchsia_tests.md
[scenic]: /docs/concepts/graphics/scenic/scenic.md
[contribute-changes]: /docs/development/source_code/contribute_changes.md

