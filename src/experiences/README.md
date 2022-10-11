Fuchsia Experiences Repository
==============================

This is a companion repository to Fuchsia and contains code that either
implements or supports user facing code for the Fuchsia platform. The code
provides a majority of the user interface for the Workstation product as well as
a small set of examples, tests, and benchmarks.

# Build & Run

This repository is a "source petal" included in the Fuchsia Platform Source Tree
(FPST) checkout. Code in this repository must be built with Fuchsia in order to be
functional, see [the guide](https://fuchsia.dev/fuchsia-src/development/source_code) for instructions on getting the source.

## Hardware

Hardware support should be considered experimental. However, NUC's and Pixelbooks are known to work best. For details on hardware setup see: [Install Fuchsia on a Device](https://fuchsia.dev/fuchsia-src/development/hardware/paving)

## Building

Once you have functional checkout you can [configure a build](https://fuchsia.dev/fuchsia-src/development/build/fx#configure-a-build) targeting Workstation:

        fx set workstation.<board> # For options run: `fx list-boards`
        fx build

See Fuchsia's [build and pave instructions](https://fuchsia.dev/fuchsia-src/development/build/build_and_pave_quickstart) for detailed instructions.

## Running

Once built, standard Fuchsia workflows for paving, running code, and testing
apply. See: [fx workflows](https://fuchsia.dev/fuchsia-src/development/build/fx)
for detailed instructions.


