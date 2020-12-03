# VkPrimer

A cross platform test template for validation of [Magma](/docs/the-book/magma/overview.md) drivers.
## Overview

vkprimer is a template application for vulkan development on Fuchsia.  It serves:

  - As an easy way to do Vulkan-based code prototyping.

  - As a cross-platform (Fuchsia / Linux / macOS) vulkan-based template
    that can be used to compare the rendering output of all the
    respective renderers as a debugging and development aid.

  - As an introductory how-to for Vulkan development.

  - As the basis for golden-image-based testing of Magma drivers w/ skia-gold.

It was written to codify and encapsulate the central idioms of
Vulkan to facilitate a better understanding of the Vulkan API.

vkprimer reveals dependencies between different Vulkan constructs,
in its simplest form, by reviewing the constructor arguments of
each of the classes to understand what it is they depend on to
be fully initialized.

## Design

### Object Design
Operationally, aside from any educational value, vkprimer objects defined
within the *vkp* namespace serve as boilerplate vulkan state initializers.

Some class define and expose an std::shared_ptr to its underlying type.
As such, the vkp:: container can be used to initialize the underlying
vk:: container, and then fall away and be freed when no longer in use.

Constructors of objects within the vkp:: namespace have dependencies on
vk:: types only to allow customization of the vulkan pipeline wherever
desired.

### Initialization
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

### Customization

#### Builder Pattern
For some vkp:: classes such as vkp::Instance, having a builder pattern
for non-default state is warranted because initialization does not
heavily depend on other ivars that are part of the vkp:: objects
definition.

vkp::CommandBuffers is a good example of when a builder pattern is less
desirable.  It depends on vk::Framebuffer, vk::Renderpass, vk::CommandPool
and vk::Pipeline.  Initialization of a command buffer requiring references
to all of these dependencies is messy and adds little value over just
defining the raw vk::CommandBuffers outside of vkp:: conventions.

#### Constructor Dependencies
Further motivation for defining and exposing underlying vk:: types
as std::shared_ptrs is to allow better customization of the vulkan
pipeline.  Anywhere a std::shared_ptr type is used as a dependency
during construction provides orthogonality from vkp:: types and
simplifies customization of the vulkan pipeline.

### Platform Specifics
When presentable (onscreen) surfaces are required, platform specifics
come into play.  Vkprimer uses GLFW to provide surface management
on non-Fuchsia platforms and image pipe for surface management on
Fuchsia.  When GLFW is required, USE_GLFW must be defined in the build.
