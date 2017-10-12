# Units and Metrics

This document defines the system of units and metrics used by the Fuchsia
graphics system.

[TOC]

## Goals

The purpose of this system of units and metrics is to solve the following
problems:

* Define a device-specific unit, the **pixel (px)**, used to describe
  low-level characteristics of targets or sources of image data.

* Define a physical unit, the **millimeter (mm)**, used as a physical basis
  for scale calculations.

* Define a scalable unit, the **grid (gr)**, used by UI frameworks for layout
  purposes.  Its scale is derived from physical quantities, well-defined
  configuration parameters, and through the use of empirical models to ensure
  a consistent visual impact and to optimize usability for a user or group of
  users across a broad range of devices and viewing environments.

* Help creators develop intuition about the perceptual significance of
  individual measurements expressed in logical units.  For example, **14 gr**
  might be generally a good number for small readable text.  Please refer to the
  user interface design guidelines for the actual numbers.

* Allow software to determine how many pixels to render for optimum fidelity.

* Allow software to determine known physical relations.  For example, it is
  possible to draw a graduated ruler accurately when the display's physical
  size and density are accurately known.

## System of Units

Fuchsia's graphics systems uses a few units of measure for distinct purposes,
as summarized in this table.

| Name and Notation   | Definition      | Purpose                  |
|---------------------|-----------------|--------------------------|
| **Pixel (px)**      | Device-specific | Rendering and Sampling   |
| **Millimeter (mm)** | Physical        | Scale Factor Calibration |
| **Grid (gr)**       | Scalable        | Layout Position and Size |

The following sections describe each unit in more detail.

### Pixels (px)

The **pixel** is a device-specific unit of length for expressing dimensions
as a range of addressable picture elements for a particular device.
For the purposes of this definition, a **device** is considered to be any
target or source of image data such as a display, a camera, or a texture.

It is common to express the size of a planar graphical object in the device's
coordinate system in terms of its width and height in whole or fractional pixel
units.  Similarly it is common to use pixel units to express positions and
vectors in that space.

Pixel units are not used to describe depth or elevation.

A **pixel** can also mean a single addressable picture element which measures
exactly **1 px** wide by **1 px** high.

#### Details

Fuchsia's graphics system uses **pixel units** when performing device-specific
graphical operations such as when rendering a scene, drawing text, decoding a
video, or sampling from a texture.

Pixel units should not be used directly for user interface layout because they
are not scalable and therefore cannot adapt across devices; use **grid units**
instead.

Pixel units may have different physical manifestations depending on the device
they relate to.  It is common for all pixel units of a given device to be of
the same physical size and to have a square aspect ratio but this may not
be true for some devices.

#### Examples

* A 1080p display operating at its native resolution is **1920 px** wide by
  **1080 px** high.  Assuming each pixel is encoded in 32 bits, a linear frame
  buffer for this display would require a total of 8294400 bytes
  (1920 x 1080 x 4).

* A single frame of YV12 encoded 720p video is **1280 px** wide by
  **720 px** high although its effective color resolution is lower due to the
  use of chroma subsampling.

### Millimeters (mm)

The **millimeter** is a standard unit of length for expressing the physical
dimensions and spatial relations of real world objects and their analogues.
It is equivalent to 1/1000th of a **meter** as defined by the International
System of Units (SI).

It is common to express the size of physical objects in whole or fractional
millimeter units or as ratios involving millimeters such as **pixels per
millimeter (px/mm)**.

#### Details

Fuchsia's graphics system uses known physical measurements in **millimeters** to
calibrate other units, such as the **grid** (see below).  When these physical
measurements are not known, the system will use different formulations to
compensate for the lack of this information.

Millimeters are commonly used to express physical relationships with other units
in the form of ratios, such as the number of pixels per millimeter of a display.

Millimeters should not be used directly for user interface layout because they
do not capture the perceptual effects of viewing distance and other usability
concerns; use **grid unit** instead.

#### Examples

* One particular display might have an active area that is **257.8 mm** high by
  **171.9 mm** wide with a pixel density of **8.4 px/mm**.

* The nominal viewing distance of that particular display in a typical viewing
  environment might be approximately **500 mm**.

### Grid Units (gr)

The **grid** is a device-independent scalable unit of length for layout of user
interfaces and other graphical content in Fuchsia.  Its purpose is to ensure a
consistent visual impact and to optimize usability for a user or group of users
across a broad range of devices and viewing environments.

It is common to express the size of an idealized planar or volumetric graphical
object in terms of its width, height, and depth in whole or fractional grid
units.  Similarly it is common to use grid units to express positions and
vectors in that space.

The grid unit has a **equilateral cube aspect ratio**: objects whose width,
height, and depth have equal dimension in grid units will have an equal
apparent width, height, and depth when rendered to the output device.

A **grid square** is a square which measures **1 gr** wide by **1 gr** high.

A **grid cube** is a cube which measures **1 gr** wide by **1 gr** high by
**1 gr** deep.

Grid units are used at design time and on the device at run time to provide
a form of scale invariance.

* At design time, the developer uses grid units to describe the size and
  position of idealized graphical objects based on the user interface
  design guidelines.

* At run time, the system dynamically calculates an appropriate
  **grid transformation* to map grid units to pixel units for each output
  device.  This transformation takes into account the device pixel density,
  nominal viewing distance, and other factors to maintain a consistent
  visual impact across a range of configurations.  It is adjusted as needed
  whenever any of its underlying factors changes.

* Changes in the grid transformation affect the level of detail required
  to maintain graphical fidelity.  For example, if the grid to pixel ratio
  increases by a factor of two, then a view may need to allocate textures
  twice as many pixels wide and tall to prevent content from becoming blurry
  at that scale.  The view's node metrics provide the necessary information
  to determine the required level of detail.

* Because the scene graph is dimensioned in scalable units, its overall
  layout is invariant under camera movements; only the level of detail
  changes.  This would not be true if the scene graph were dimensions in pixels.

* By convention, the root node of every view is dimensioned in grid units.
  This makes it easy to generate scene content for layouts which are
  specified in grid units since they are one-to-one with the view's local
  coordinate system in the scene graph.

#### Details

Fuchsia's graphics system uses *grid units* extensively for layout in the
scene graph and applies a transformation at rendering time.

The grid transformation is a combination of the following factors.

* Aspect ratio correction: Preserves equal apparent width, height, and depth
  for objects of equal width, height, and depth in grid units.

* Angular size correction: Adapts the scale of objects to a common resolution-
  independent baseline taking into account the physical pixel density and the
  nominal viewing distance.  Although grid units scale proportionally with
  angular resolution, other corrections cause them not to have a constant
  apparent angular size in practice.

* Ergonomic correction: Adapts the scale of objects to compensate for the
  information architecture needs of particular classes of devices due to how
  they are intended to be used, allowing for the presentation of more or less
  information in the same canvas.

* Perceptual correction: Adapts the scale of objects to compensate for
  perceptual effects which occur based on the user's context and viewing
  environment.

* User correction: Adapts the scale of objects to compensate for user
  preferences such as their accessibility needs.  This term has no effect
  when default user settings are in effect.

See [Display Metrics](ui_display_metrics.md) for more details about how
the grid transformation is actually determined and used.

#### Examples

* **1 gr** on a handheld information device typically used at arm's length with
  default settings corresponds to a visual angle of approximately 0.025 degrees.
  This is similar to the **density-independent pixel (dp)** unit.

* By comparison, the [CSS Reference Pixel](https://www.w3.org/TR/css-values-3/#reference-pixel)
  is defined to have a visual angle of 0.0213 degrees.

## Metrics

Fuchsia's graphics system provides APIs for programs to access scale factors,
physical dimensions, and other information essential to adapting graphical
output for a particular rendering context.

These properties are collectively known as **Metrics** and are summarized
in the following tables.

### Display Metrics

Display metrics describe the physical characteristics of a particular display
and its basic scale factors.

| Name            | Unit  | Definition                                |
|-----------------|-------|-------------------------------------------|
| Grid Width      | gr    | Width of visible content area             |
| Grid Height     | gr    | Height of visible content area            |
| Grid Scale X    | px/gr | Nominal pixels per grid in X              |
| Grid Scale Y    | px/gr | Nominal pixels per grid in Y              |
| Grid Density    | gr/mm | Grid density (optional)                   |
| Display Width   | px    | Width of visible content area             |
| Display Height  | px    | Height of visible content area            |
| Physical Width  | mm    | Width of visible content area (optional)  |
| Physical Height | mm    | Height of visible content area (optional) |

### View Metrics

View metrics describe the local layout constraints of an individual user
interface component based on the view is embedded into the view hierarchy.

Views receive this information at runtime in the form of **ViewProperties**.
View properties may change dynamically in response to view hierarchy changes
which affect the view's layout.

| Name               | Unit  | Definition                  |
|--------------------|-------|-----------------------------|
| View Width         | gr    | Width constraint in grids   |
| View Height        | gr    | Height constraint in grids  |
| View Max Elevation | gr    | Maximum elevation in grids  |

The view is generally expected to layout its content so as to fill the
available width and height at elevation zero.

Since views can be three dimensional, the maximum elevation places an upper
bound on the elevation of the airspace which the view is allowed to use.

### Node Metrics

Node metrics describe the local rendering context of a node in the scene graph
based on the characteristics of the rendering target into which the node is
projected and the transformations applied by the node's ancestors.

Nodes receive this information at runtime in the form of **MetricsEvents**.
Node metrics may change dynamically in response to scene graph changes
which affect the node's projection into the rendering target.

| Name         | Unit  | Definition                           |
|--------------|-------|--------------------------------------|
| Grid Scale X | px/gr | Nominal pixels per grid in X         |
| Grid Scale Y | px/gr | Nominal pixels per grid in Y         |
| Grid Density | gr/mm | Physical grid density (0 if unknown) |

The grid scale factor is important for deciding the resolution of textures
needed to achieve optimum fidelity on the rendering target.  For example, given
a uniform grid scale factor of **2.5**, the ideal texture size to fill a
a **150 gr** by **100 gr** rectangle is **375 px** by **250 px**.

The grid density is useful for mapping grid dimensions to or from physical
dimensions, although this may not be possible if the rendering target's
physical resolution is unknown.

These metrics are designed to consider the overall context of the node in the
scene graph.  For example, if an ancestor of the node applies a 200% scale
transformation to the subtree containing the node, then the node will receive
an updated metrics event containing values which are scaled up by 200%.
This informs the node that it may need to allocate higher resolution textures
to maintain optimum fidelity.

TODO(MZ-378): Node metrics currently do not take into account the effects
of certain transformations such as perspective projections and rotations
which could affect the necessary level of detail required to maintain optimum
fidelity or the accuracy of physical registration.  We should consider
introducing additional factors in Scenic to help estimate these effects.

## Model Parameters

The Fuchsia graphics system automatically calculates metrics based on an
empirical model using information about a display's physical characteristics,
its viewing environment, user preferences, and other contextual cues.

For this model to function correctly, it needs accurate parameters.

In particular, when a model parameter is not accurately known a priori, such
as the display's exact physical size and pixel density, then it must be
reported as having an unknown value.

Do not provide fake or poorly estimated input parameters; report them as
unknown instead.  The display model is responsible for choosing sensible
defaults based on what actually is known.

In some situations, it may be appropriate to prompt the end-user to supply
missing information during setup to optimize fidelity.

Refer to the **DisplayModel** class for more details.

TODO(MZ-379): Document specific calibration procedures and expected accuracy
bounds for each model parameter.

### Display Information

The display information describes the physical characteristics of a particular
display.

| Name            | Unit  | Definition                                |
|-----------------|-------|-------------------------------------------|
| Display Width   | px    | Width of visible content area             |
| Display Height  | px    | Height of visible content area            |
| Physical Width  | mm    | Width of visible content area (optional)  |
| Physical Height | mm    | Height of visible content area (optional) |
| Pixel Density   | px/mm | Pixel density of active area (optional)   |

### Environment Information

The environment information describes the physical characteristics of how a
display is typically used and perceived in a given environment.

| Name             | Unit  | Definition                                   |
|------------------|-------|----------------------------------------------|
| Usage            | usage | Intended usage of the display (optional)     |
| Viewing Distance | mm    | Nominal apparent viewing distance (optional) |

The usage classification expresses how a particular display is intended to
be used in a given context.  This information helps the system select
appropriate defaults and adjust the information architecture to suit the role
of that display.

* **Unknown**: The role of the display is unknown.
* **Handheld**: The display is mounted in a device which is typically supported
  by the user in one or both hands.  The user interface will be optimized for
  single-user direct manipulation.  Like a phone or tablet.
* **Close**: The display is mounted in a device which is typically located
  well within arm's reach of the user.  The user interface will be optimized
  for single-user direct and indirect manipulation.  Like a laptop.
* **Near**: The display is mounted in a device which is typically located
  at arm's reach from the user.  The user interface will be optimized for
  single-user indirect manipulation.  Like a desktop.
* **Far**: The display is mounted in a device which is typically located well
  beyond arm's reach of the user or a group of users and is intended to be
  viewed from a variety of distances.  The user interface will be optimized for
  single-user and multi-user interaction and media consumption.  Like a TV.

The viewing distance estimates how far away objects presented on the display at
a zero elevation will appear to the user.

### User Information

The user information describes the user's preferences and accessibility needs
which may override some of the behavior of the model.

| Name              | Unit  | Definition                           |
|-------------------|-------|--------------------------------------|
| User Scale Factor | gr/gr | Magnification Ratio (default is 1.0) |

The user scale factor allows users to uniformly scale the entire user interface
by multiplying the grid scale factor with a user specified ratio.  This has the
effect of increasing or decreasing the apparent angular size of graphical
objects and correspondingly decreasing or increasing the amount of available
space for layout in grid units.
