# Explore Fuchsia {#explore-fuchsia}

In Fuchsia, [components][components] are the basic unit of executable software.

## Run an example component {#run-an-example-component}

To run an example component on your Fuchsia device, see the
[Run an example component][run-examples] guide.

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

## Write software for Fuchsia {#write-software-for-fuchsia}

[FIDL][fidl] (Fuchsia Interface Definition Language) is the Interprocess
Communication (IPC) system for Fuchsia.

To learn more about FIDL, the following resources are available:

*   To get a brief overview of what FIDL is, including its design goals,
    requirements, and workflows, read the [FIDL concepts][fidl-concepts] guide.
*   To learn how to write FIDL APIs and client and server components, review the
    [FIDL tutorials][fidl-tutorials].

## Run Fuchsia tests {#run-fuchsia-tests}

To test Fuchsia on your device, see the [Run Fuchsia tests][run-fuchsia-tests]
guide.

## Launch a graphical component {#launch-a-graphical-component}

Most graphical components in Fuchsia use the [Scenic][scenic] system compositor.
You can launch such components (commonly found in `/system/apps`) using the
`present_view` command, for example:

```posix-terminal
fssh present_view fuchsia-pkg://fuchsia.com/spinning_square_view#meta/spinning_square_view.cmx
```

For more information, see [Scenic example apps](/src/ui/examples).

If you launch a component that uses Scenic or hardware-accelerated graphics,
Fuchsia enters the graphics mode, which doesn't display the shell. To use the
shell, press `Alt+Escape` to enter the console mode. Press `Alt+Escape` again to
return to the graphics mode.

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

