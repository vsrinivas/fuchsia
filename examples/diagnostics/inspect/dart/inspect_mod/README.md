# Inspect Dart

<!-- TODO(fxbug.dev/84446): Migrate this example to use CFv2 -->

A simple module that demonstrates usage of the Dart Inspect API.

## Running example app

Assumes you already have an existing Fuchsia checkout and have set-up your hardware.

1.  Run for workstation.

    ```
    fx set workstation.chromebook-x64 --with //bundles:kitchen_sink
    ```

1.  Do a fresh build.

    ```
    fx build
    ```

1.  Connect and turn on the desired device. Serve to hardware.

    ```
    fx serve
    ```

1.  (If this is your first time building the example app) You may need to run:

    ```
    fx ota
    ```

    in order to have access to iquery.

1.  (Once the UI comes up) Login as a Guest user.

    Click on the (+) icon and select "Guest". This only needs to be done once on
    initial setup.

1.  Add the Inspect mod.

    ```
    fx shell sessionctl add_mod fuchsia-pkg://fuchsia.com/inspect_mod#meta/inspect_mod.cmx
    ```

    You should see the mod appear on the device.

1.  View iquery output. This saves a step of looking for the mod because we know
    the mod is "inspect_mod.cmx":

    ```
    fx shell iquery show inspect_mod.cmx
    ```

## Local development

Each time you make local Dart changes and want to see the changes, you'll want
to rebuild and re-add the inspect mod.

1.  Press "x" on the open mod to close it.

1.  Build and add your mod.

    ```
    fx build && fx shell sessionctl add_mod fuchsia-pkg://fuchsia.com/inspect_mod#meta/inspect_mod.cmx
    ```
