# Magma: Porting Guide

For an overview of Magma including background, hardware requirements, and description of architecture, please see [Magma: Overview](README.md).

For each component, a short term and long term process is described, where, in the short term, all Fuchsia related development is performed by the Magma team.

## Magma system driver

The magma system driver must be open source but not GPL and hosted on *fuchsia.googlesource.com*.  A magma system driver must provide an implementation of the [msd interface](/garnet/lib/magma/include/msd_abi/msd.h).

### Short term

The Magma team writes new code, supporting only the latest gpu hardware generations. Some combination of the following resources are required:

* Hardware documentation (register spec, theory of operation)
* A reference implementation (Linux)
* Vendor support

### Long term

The gpu vendor supplies and maintains the system driver using the Zircon DDK.

### Tasks

* Initialize hardware: register access, clocks, regulators, interrupts, firmware.  **Note** where the GPU block is agnostic of these concerns, they should be configured in a separate board driver; see Zircon [platform-bus](/docs/concepts/drivers/platform-bus.md).
	* *msd_driver_create*
	* *msd_driver_configure*
	* *msd_driver_destroy*
	* *msd_driver_create_device*
	* *msd_device_destroy*
* Support for parameter querying
	* *msd_device_query*
* Create connections
	* *msd_device_open*
	* *msd_connection_close*
* Create buffers
	* *msd_buffer_import*
	* *msd_buffer_destroy*
* Set up memory spaces and buffer mappings
	* *msd_connection_map_buffer_gpu*
	* *msd_connection_unmap_buffer_gpu*
	* *msd_connection_commit_buffer*
	* *msd_connection_release_buffer*
* Set up hardware contexts
	* *msd_connection_create_context*
	* *msd_context_destroy*
* Command buffer scheduling
	* *msd_context_execute_command_buffer*
	* *msd_context_execute_immediate_commands*
	* *msd_connection_set_notification_callback*
* Create semaphores
	* *msd_semaphore_import*
	* *msd_semaphore_destroy*
* Support for status dump
	* *msd_device_dump_status*
* Fault handling
* Power management

## Client side library

Not required to be open source; for bringup, the repo may be hosted by the Magma team internally and only the binary objects will be distributed.

The client driver library should provide a conformant implementation of Vulkan 1.0/1.1.  It must also implement several Fuchsia specific variants of common KHR Vulkan extensions for external memory and semaphores. These are currently WIP and subject to change, but can be found in the Fuchsia internal [Vulkan header](https://fuchsia.googlesource.com/third_party/vulkan_loader_and_validation_layers/+/master/include/vulkan/vulkan.h):

* VK_FUCHSIA_external_memory
* VK_FUCHSIA_external_semaphore

### Short term
The Magma team pulls codebase updates from the vendor periodically.  The Fuchsia customization work must be then pushed back upstream to the vendor to ease the burden of future merges.

### Long term
Eventually, the vendor will be able to build and test for Fuchsia, so the Fuchsia port can be handled entirely by the vendor like any other supported operating system.

### Bring-up Tasks

* Build the vendor code
* If closed source, make a static library as a distributable prebuilt that can be linked with dependencies in fuchsia to make a complete shared library that can be loaded into the application’s process space
* Port any os dependencies to Fuchsia (Fuchsia provides a c library with a lot of posix support and a std c++ library)
* Rework system integration layer to use magma interfaces instead of kernel interfaces
* Implement Fuchsia Vulkan extensions

### Validation Stages

* A simple Vulkan test passes
	* Test: [vkreadback](/src/graphics/tests/vkreadback) (draws a color then reads back the framebuffer values)
* Add support for fuchsia window system integration extensions using zircon framebuffer library
    * Test: [vkcube](/src/graphics/examples/vkcube/) (animated, using VK_KHR_swapchain)
* Add support for fuchsia external memory and semaphore extensions
	* Test: [vkext](/src/graphics/tests/vkext)

