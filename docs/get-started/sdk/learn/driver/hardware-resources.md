# Configure hardware resources

Peripheral Component Interconnect (PCI) devices expose resources to the system
using a variety of interfaces including Interrupts, Memory-Mapped I/O (MMIO)
registers, and Direct Memory Access (DMA) buffers. Fuchsia drivers access these
resources through capabilities from the parent device node. For PCI devices,
the parent offers an instance of the `fuchsia.hardware.pci/Device` FIDL protocol
to enable the driver to configure the device.

In this section, you'll be adding functionality to access the following MMIO
registers on the `edu` device:

Address offset | Register              | R/W | Description
-------------- | --------------------- | --- | -----------
0x00           | Identification        | RO  | Major / minor version identifier
0x04           | Card liveness check   | RW  | Challenge to verify operation
0x08           | Factorial computation | RW  | Compute factorial of the stored value
0x20           | Status                | RW  | Bitfields to signal the operation is complete

Note: For complete details on the `edu` device and its MMIO regions, see the
[device specification][edu-device-spec].

After you complete this section, the project should have the following directory
structure:

```none {:.devsite-disable-click-to-copy}
//fuchsia-codelab/qemu_edu/drivers
                  |- BUILD.bazel
                  |- meta
                  |   |- qemu_edu.cml
{{ '<strong>' }}                  |- driver_compat.h {{ '</strong>' }}
                  |- qemu_edu.bind
                  |- qemu_edu.cc
                  |- qemu_edu.h
{{ '<strong>' }}                  |- registers.h {{ '</strong>' }}
```

## Connect to the parent device

To access the `fuchsia.hardware.pci/Device` interface from the parent device
node, add the `fuchsia.driver.compat.Service` capability to the driver's
component manifest:

`qemu_edu/drivers/meta/qemu_edu.cml`:

```json5
{
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/meta/qemu_edu.cml" region_tag="driver" %}
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/meta/qemu_edu.cml" region_tag="use_capabilities" exclude_regexp="protocol" highlight="1,2,3" %}
}
```

This enables the driver to open a connection to the parent device and access the
hardware-specific protocols it offers.

Create the `qemu_edu/drivers/driver_compat.h` file and add the following code to
use the `fuchsia.driver.compat.Service` capability to open the device connection:

`qemu_edu/drivers/driver_compat.h`:

```cpp
#ifndef FUCHSIA_CODELAB_QEMU_EDU_DRIVER_COMPAT_H_
#define FUCHSIA_CODELAB_QEMU_EDU_DRIVER_COMPAT_H_

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/driver_compat.h" region_tag="imports" adjust_indentation="auto" exclude_regexp="fuchsia\.device\.fs" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/driver_compat.h" region_tag="namespace_start" adjust_indentation="auto" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/driver_compat.h" region_tag="connect_parent" adjust_indentation="auto" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/driver_compat.h" region_tag="namespace_end" adjust_indentation="auto" %}

#endif  // FUCHSIA_CODELAB_QEMU_EDU_DRIVER_COMPAT_H_

```

Update the driver's `Run()` method to access the `fuchsia.hardware.pci/Device`
offered by the parent device during driver initialization:

`qemu_edu/drivers/qemu_edu.cc`:

```cpp
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="imports" adjust_indentation="auto" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="compat_imports" adjust_indentation="auto" highlight="1" %}

// ...

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="run_method_start" adjust_indentation="auto" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="connect_device" highlight="1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18" %}

  FDF_SLOG(INFO, "edu driver loaded successfully");

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="run_method_end" adjust_indentation="auto" %}
```

## Set up interrupts and MMIO

With a connection open to the `fuchsia.hardware.pci/Device`, you can begin to
map the necessary device resources into the driver. Add the following code to
your driver class to declare a new `MapInterruptAndMmio()` method:

`qemu_edu/drivers/qemu_edu.h`:

```cpp
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.h" region_tag="imports" adjust_indentation="auto" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.h" region_tag="hw_imports" adjust_indentation="auto" highlight="1,2,3" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.h" region_tag="namespace_start" adjust_indentation="auto" %}

class QemuEduDriver {
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.h" region_tag="public_main" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.h" region_tag="private_main" %}
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.h" region_tag="private_hw" highlight="1" %}
  
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.h" region_tag="fields_main" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.h" region_tag="fields_hw" highlight="1,2" %}
};

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.h" region_tag="namespace_end" adjust_indentation="auto" %}
```

This method performs the following tasks:

1.  Access the Base Address Register (BAR) of the appropriate PCI region.
1.  Extract Fuchsia's [VMO][concepts-kernel-vmo] (Virtual Memory Object) for
    the region.
1.  Create an MMIO buffer around the region to access individual registers.
1.  Configure an Interrupt Request (IRQ) mapped to the device's interrupt.

Add the following code to implement the `MapInterruptAndMmio()` method:

`qemu_edu/drivers/qemu_edu.cc`:

```cpp
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="namespace_start" adjust_indentation="auto" %}
// ...

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="interrupt_mmio" adjust_indentation="auto" highlight="1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="namespace_end" adjust_indentation="auto" %}
```

Update the driver's `Run()` method to call the new method during driver
initialization:

`qemu_edu/drivers/qemu_edu.cc`:

```cpp
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="run_method_start" adjust_indentation="auto" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="connect_device" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="hw_resources" highlight="1,2,3,4,5" %}

  FDF_SLOG(INFO, "edu driver loaded successfully");

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="run_method_end" adjust_indentation="auto" %}
```

Update the driver build configuration to depend on the FIDL binding libraries
for these two protocols:

`qemu_edu/drivers/BUILD.bazel`:

```bazel
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/BUILD.bazel" region_tag="binary" adjust_indentation="auto" exclude_regexp="registers\.h|driver_compat\.h|\/\/src\/qemu_edu|sdk\/\/fidl\/fuchsia\.device" highlight="9,10" %}
```

## Read device registers

With the base resources mapped into the driver, you can access individual
registers. Create the new `qemu_edu/drivers/registers.h` file in your project
directory with the following contents:

`qemu_edu/drivers/registers.h`:

```cpp
#ifndef FUCHSIA_CODELAB_QEMU_EDU_REGISTERS_H_
#define FUCHSIA_CODELAB_QEMU_EDU_REGISTERS_H_

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/registers.h" region_tag="imports" adjust_indentation="auto" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/registers.h" region_tag="namespace_start" adjust_indentation="auto" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/registers.h" region_tag="register_definitions" adjust_indentation="auto" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/registers.h" region_tag="namespace_end" adjust_indentation="auto" %}

#endif  // FUCHSIA_CODELAB_QEMU_EDU_REGISTERS_H_

```

This file declares the register offsets provided in the device specification as
constants. Fuchsia's `hwreg` library wraps the registers that represent
bitfields, making them easier to access without performing individual bitwise
operations.

Add the following to the driver's `Run()` method to read the major/minor version
from the identification register from the MMIO region and print it to the log:

`qemu_edu/drivers/qemu_edu.cc`:

```cpp
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="imports" adjust_indentation="auto" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="compat_imports" adjust_indentation="auto" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="hw_imports" adjust_indentation="auto" highlight="1" %}

// ...

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="run_method_start" adjust_indentation="auto" %}
  // ...

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="hw_resources" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="device_registers" highlight="1,2,3,4" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/qemu_edu.cc" region_tag="run_method_end" adjust_indentation="auto" %}
```

Update the driver's build configuration to include the new includes as source
files:

`qemu_edu/drivers/BUILD.bazel`:

```bazel
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/qemu_edu/drivers/BUILD.bazel" region_tag="binary" adjust_indentation="auto" exclude_regexp="\/\/src\/qemu_edu|sdk\/\/fidl\/fuchsia\.device" highlight="6,7" %}
```

<<_common/_restart_femu.md>>

## Reload the driver

Use the `bazel run` command to build and execute the component target:

```posix-terminal
bazel run --config=fuchsia_x64 //fuchsia-codelab/qemu_edu/drivers:pkg.component
```

The `bazel run` command rebuilds the package and runs `ffx driver register` to
reload the driver component.

Inspect the system log and verify that you can see the updated `FDF_SLOG()`
message containing the version read from the identification register:

```posix-terminal
ffx log --filter qemu_edu
```

```none {:.devsite-disable-click-to-copy}
[driver_manager][driver_manager.cm][I]: [driver_runner.cc:959] Binding fuchsia-pkg://bazel.pkg.component/qemu_edu#meta/qemu_edu.cm to  00_06_0_
{{ '<strong>' }}[universe-pkg-drivers:root.sys.platform.platform-passthrough.PCI0.bus.00_06_0_][qemu-edu,driver][I]: [fuchsia-codelab/qemu_edu/qemu_edu.cc:75] edu device version major=1 minor=0 {{ '</strong>' }}
```

Congratulations! Your driver can now access the PCI hardware resources provided
by the bound device node.

<!-- Reference links -->

[concepts-kernel-vmo]: /docs/concepts/kernel/concepts.md#shared_memory_virtual_memory_objects_vmos
[edu-device-spec]: https://fuchsia.googlesource.com/third_party/qemu/+/refs/heads/main/docs/specs/edu.txt
