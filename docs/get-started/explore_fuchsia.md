# Explore Fuchsia {#explore-fuchsia}

When Fuchsia is booted and displays the `$` prompt in the shell, you can now run
[components](/docs/concepts/components/v2). In Fuchsia, components are the basic
unit of executable software.

To run components on your Fuchsia device, see
[Run an example component](/docs/development/run/run-examples.md).

## Run shell commands

To shutdown or reboot Fuchsia, use the following `dm` commands in the shell:

```sh
dm shutdown
dm reboot
```

See
[Connect to a target shell](/docs/development/build/fx.md#connect-to-a-target-shell)
for more information.

## Select a tab {#select-a-tab}

Fuchsia shows multiple tabs in the shell. At the top of the screen, the
currently selected tab is highlighted in yellow.

The following keyboard shortcuts help you navigate the terminal:

-   Alt+Tab switches between tabs.
-   Alt+F{1,2,...} switches directly to a tab.
    -   Tab zero is the console, which displays the boot and component log.
    -   Tabs 1, 2 and 3 contain shells.
    -   Tabs 4 and higher contain components you've launched.
-   Alt+Up/Down scrolls up and down by lines.
-   Shift+PgUp/PgDown scrolls up and down by half page.
-   Ctrl+Alt+Delete reboots.

## Write software for Fuchsia

For an example of writing [FIDL](/docs/development/languages/fidl) APIs and client
and server components, review the
[FIDL tutorials](/docs/development/languages/fidl/tutorials/overview.md)

## Run tests

To test Fuchsia on your device, see
[Run Fuchsia tests](/docs/development/testing/run_fuchsia_tests.md).

## Launch a graphical component

Most graphical components in Fuchsia use the
[Scenic](/docs/concepts/graphics/scenic/scenic.md) system compositor. You can
launch such components (commonly found in `/system/apps`) using the
`present_view` command, for example:

```sh
present_view fuchsia-pkg://fuchsia.com/spinning_square_view#meta/spinning_square_view.cmx
```

See [Scenic example apps](/src/ui/examples).

If you launch a component that uses Scenic or hardware-accelerated graphics,
Fuchsia enters the graphics mode, which doesn't display the shell. To use the
shell, press `Alt+Escape` to enter the console mode. In the console mode,
`Alt+Tab` has the same behavior described in [Select a tab](#select-a-tab).
Press `Alt+Escape` again to return to the graphics mode.

## Contribute changes

To submit your contribution to Fuchsia, see
[Contribute changes](/docs/development/source_code/contribute_changes.md).

## See also

*   [fx workflows](/docs/development/build/fx.md)
*   [Workflow tips and questions](/docs/development/source_code/workflow_tips_and_faq.md)
*   [Configure editors](/docs/development/editors/)
*   [Source code layout](/docs/concepts/source_code/layout.md)
*   [Build system](/docs/concepts/build_system/index.md)
