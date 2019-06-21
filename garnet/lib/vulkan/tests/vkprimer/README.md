# VkPrimer

A cross platform test template for validation of [Magma](/docs/the-book/magma/overview.md) drivers.
## Overview

vkprimer is a template application for vulkan development on Fuchsia.  It serves:

  - As the basis for golden-image-based testing of Magma drivers w/ skia-gold.

  - As a cross-platform (Fuchsia / Linux / macOS) vulkan-based template
    that can be used to compare the rendering output of all the
    respective renderers as a debugging and development aid.

  - As an introductory how-to for Vulkan development on Fuchsia.

It was written to codify and encapsulate the central idioms of
Vulkan to facilitate a better understanding of the Vulkan API.

vkprimer reveals dependencies between different Vulkan constructs,
in its simplest form, by reviewing the constructor arguments of
each of the classes to understand what it is they depend on to
be fully initialized.

Init() methods are a central theme within the classes.  These
methods do the lion's share of the work to initialize any given
instance.  This simplifies constructors and allows deferred loading
strategies to better manage start-up time.

It is the responsibility of the Init() methods to release initialization
parameters ("InitParams") that are constructed in the constructors of
each of the classes that rely on them.  These are temporary parameters
that contain state that exist between object construction through the
end of the call to Init() on that object.  InitParams are expected to
have no subsequent use beyond initializing the object.

frag.spv and vert.spv were compiled using LunarG's
glslangValidator app on debian.
