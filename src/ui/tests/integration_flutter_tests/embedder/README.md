# Flutter Embedder Tests

## Overview

The Flutter embedder tests embed a Flutter view (*parent-view* Flutter app)
which, itself, embeds a Flutter view (*child-view* Flutter app). The tests
asserts the pixel color output from a screenshot of the root view as a method of
verifying the following:

*   State changes across clients behave as expected; and
*   Flutter's rendering code produces the correct output.

As a limitation, the embedder tests **do not test** the layout of embedded views
nor verify individual pixel colors. For more information, see
[Implementation Details](#implementation-details).

## Build and Run Instructions

### Build

Because the embedder tests use a fake display controller, they can be run on any
*product*.

```console
fx set <product>.<board> --with //src/ui/tests
fx build
```

### Run

```console
# Run Flutter Embedder Test (Input Pipeline and Scene Manager variants included)
ffx test run fuchsia-pkg://fuchsia.com/flutter-embedder-test#meta/flutter-embedder-test.cm
```

## Implementation Details

### Realm Construction

The Flutter embedder tests use the
[Realm Builder](/docs/development/testing/components/realm_builder.md) library
to construct the hermetic test realm for each test at runtime. The constructed
realm has the following topology:

```
       test_manager
            |
     <test component>
            |
       <realm root>
            |                <-Test realm
 ----------------------------
     /      |     \          <-Scenic realm
  Scenic  Hdcp  InputPipeline*

```

### Test Pattern Overview

Each test consists of the following pattern:

*   Construct the test realm. Because the test is parameterized, one variant
    will run with a test realm comprised of Root Presenter (GFX) as the scene
    owner and Input Pipeline as the input owner, and another variant will run
    with a test realm comprised of Scene Manager (GFX) as both the scene owner
    and input owner.

*   Launch and embed the parent-view component with any specified arguments.

    *   This component uses flutter runner.

    *   See [Launch Arguments](#launch-arguments) for further details about
        arguments.

*   The parent-view, in turn, launches and embeds the child-view component.

*   Register a fake touch input device into the input pipeline registry and
    create a fake input event.

*   Use Scenic to take a screenshot until a specified color is found or a
    failure timeout is reached.

    *   Because the apps may take a non-deterministic time to launch to the
        expected state, we must continuously take a screenshot.

*   Use the matching screenshot to assert state against expectations.

### Color Test

The test cases render different elements to the screen, some of which react to
touch:

*   The parent-view app consists of an embedded child-view and an optional
    overlay box.

*   The parent-view background color is **blue** when no touch has been
    registered.

*   The parent-view background color is **black** when a touch has been
    registered.

*   The child-view app is centered and overlaps some of the parent-view
    background color.

*   The child-view background color is **yellow** when no touch has been
    registered.

*   The child-view background color is **pink** when a touch has been
    registered.

*   The optional overlay box, when rendered, sits in the top-right corner and
    overlaps some of both the parent-view and child-view background colors.

*   The overlay box's color is **green**.

Each test case renders a different configuration of these elements and takes a
screenshot to verify correct rendering. Some test cases also inject a touch
event, then take another screenshot to verify the corresponding color change.

When a screenshot is taken, a histogram of the RGB color values within the
screenshot is generated (i.e. the total count of pixels for each RGB value
present in the screenshot). The histogram is used to verify that the expected
colors from those listed above are present or not present within the screenshot
as well as the **relative** frequency of the colors. Note that the test **does
not** test the color of individual pixels at an XY position in the screenshot or
total count of pixels matching an expected color.

### Launch Arguments

The parent-view can be launched with the following arguments:

*   `focusable`: Whether the child view should be allowed to receive focus
    (defaults true).
*   `hitTestable`: Whether the child view should be included during hit testing
    (defaults true).
*   `showOverlay`: Parent-view app contains a green box that overlaps some of
    both the parent-view and child-view.
*   `useFlatland`: Scenic API uses Flatland (2D API) instead of GFX (3D Scene
    Graph API).
    *   **This flag is not currently used as it has not been enabled yet in
        Flutter.**

The launch arguments are currently specified as part of the component manifest.

### Overlay Details

The Flutter renderer draws overlays as a single, large layer. Some parts of this
layer are fully transparent, so we want the compositor to treat the layer as
transparent and blend it with the contents below.

However, the gfx Scenic API only provides one way to mark this layer as
transparent which is to set the opacity as < 1.0 for the entire layer. In
practice, we use 0.9961 (254 / 255) as an opacity value to force transparency.
Unfortunately this causes the overlay to blend very slightly with both the
parent-view and child-view in the overlapped regions and current tests must
account for these colors.

Flatland allows marking a layer as transparent while still using a 1.0 opacity
value when blending, so migrating Flutter to Flatland will fix this issue. When
running on gfx all tests continue to use hard-coded broken, blended values.

Be aware, when adding new tests for opacity, that opacity can cause slightly
divergent pixel values on different GPUs.
