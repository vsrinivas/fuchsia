# Get started with driver development

This guide provides step-by-step instructions that walk you through the basic
workflows of building, running, debugging, and updating
[drivers][driver-concepts] in a Fuchsia system using the
[Fuchsia SDK][using-the-sdk].

Important: This guide is the driver equivalent of the
[_Get started with the Fuchsia SDK_][get-started-sdk] guide. If you haven't
already, it's strongly recommended that you first complete the _Get started
with the Fuchsia SDK_ guide to become familiar with the comprehensive set of
Fuchsia SDK-based workflows.

Complete the following sections:

1. [Prerequisites](#prerequisites).
2. [Clone the SDK driver samples repository](#clone-the-sdk-driver-samples-repository).
3. [Start the emulator](#start-the-emulator).
4. [Build and load the sample driver](#build-and-load-the-sample-driver).
5. [Build and run a tool](#build-and-run-a-tool).
6. [Debug the sample driver](#debug-the-sample-driver).
7. [Modify and reload the sample driver](#modify-and-reload-the-sample-driver).

Found an issue? Please [let us know][sdk-bug]{:.external}.

## 1. Prerequisites {:#prerequisites}

This guide requires that your host machine meets the following criteria:

*  An x64-based machine running Linux or macOS.

   Note: While you can use an x64-based (Intel) macOS machine for this get-started
   flow, you might run into issues. To help us improve, please
   [file a bug][sdk-bug]{:.external} if you discover issues on macOS.

*  Has at least 15 GB of storage space.
*  Supports [KVM][kvm]{:.external} (Kernel Virtual Machine) for running a
   [QEMU][qemu]{:.external}-based emulator.
*  IPv6 is enabled.
*  [Git][git-install]{:.external} is installed.

## 2. Clone the SDK driver samples repository {:#clone-the-sdk-driver-samples-repository}

Clone the [SDK driver samples repository][sdk-driver-sample-repo]{:.external}
on your host machine. This repository contains sample driver components and the
Bazel-based Fuchsia SDK.

The tasks include:

*   Bootstrap the SDK driver samples repository.
*   Verify that you can build the sample driver components and run `ffx`
    commands.

Do the following:

1. In a terminal, change to your home directory:

   Note: This guide uses the home directory (`$HOME`) as a base directory. This
   is where a new work directory (`drivers`) will be created for this guide. You
   may also select a different base directory (for instance,
   `cd $HOME/my-fuchsia-project`).

   ```posix-terminal
   cd $HOME
   ```

1. Clone the SDK driver samples repository:

   ```posix-terminal
   git clone https://fuchsia.googlesource.com/sdk-samples/drivers fuchsia-drivers --recurse-submodules
   ```

   This `git clone` command creates a new directory named `fuchsia-drivers` (see the
   command's second argument above) and clones the content of the SDK driver samples
   repository.

1. Go to the new directory:

   ```posix-terminal
   cd fuchsia-drivers
   ```

1. Run the bootstrap script to install Bazel and other required dependencies:

   ```posix-terminal
   scripts/bootstrap.sh
   ```

1. To verify the Fuchsia SDK environment setup, build the sample drivers:

   ```posix-terminal
   tools/bazel build --config=fuchsia_x64 //src/qemu_edu/drivers:qemu_edu
   ```

   The first build may take a few minutes to download dependencies, such as
   Bazel build rules, [Clang][clang], and [Fuchsia IDK][fuchsia-idk] (which
   includes the `ffx` tool).

   When finished successfully, it prints output similar to the following in the
   end:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/bazel build --config=fuchsia_x64 //src/qemu_edu/drivers:qemu_edu
   ...
   INFO: Elapsed time: 124.509s, Critical Path: 68.29s
   INFO: 996 processes: 607 internal, 389 linux-sandbox.
   INFO: Build completed successfully, 996 total actions
   ```

5. To verify that you can use the `ffx` tool in your environment, run the
   following command:

   ```posix-terminal
   tools/ffx sdk version
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx sdk version
   10.20221021.1.1
   ```

   At this point, you only need to confirm that you can run `ffx` commands
   without error. (However for your information, the output above shows the version
   `10.20221021.1.1`, which indicates that this SDK was built and published on
   October 21, 2022.)

   Note: To ensure that you’re using the right version of `ffx` during development,
   consider updating your `PATH` to include the SDK's `tools` directory
   (for instance, `export PATH="$PATH:$HOME/fuchsia-drivers/tools"`). However,
   if you don't wish to update your `PATH`, ensure that you specify the relative path to
   this `ffx` tool (`tools/ffx`) whenever you run `ffx` commands.

## 3. Start the emulator {:#start-the-emulator}

Start the [Fuchsia emulator][femu] on the host machine while configuring the
emulator instance to use Fuchsia’s new [driver framework][driver-framework]
(DFv2).

The tasks include:

*   Download Fuchsia's Workstation prebuilt image from Google Cloud Storage.
*   Start the Fuchsia emulator.
*   Set the emulator instance as your host machine’s default target device.
*   Start the Fuchsia package server.
*   Register the system package repository to the emulator instance.

Do the following:

1. Download the latest Workstation image for the emulator:

   ```posix-terminal
   tools/ffx product-bundle get workstation_eng.qemu-x64 --repository workstation-packages
   ```

   This command may take a few minutes to download the image and product
   metadata.

   Note: If the `product-bundle` command fails with an error due to multiple product bundle
   instances or SDK versions, [clean up the environment](#clean-up-the-environment) before
   proceeding.

   Once the download is finished, the `ffx product-bundle get` command creates
   a local Fuchsia package repository named `workstation-packages` on your host machine.
   This package repository hosts additional system packages for this Workstation prebuilt image.
   Later in Step 8 you’ll register this package repository to the emulator instance.

1. Stop all emulator instances:

   ```posix-terminal
   tools/ffx emu stop --all
   ```

1. Start the Fuchsia emulator:

   ```posix-terminal
   tools/ffx emu start workstation_eng.qemu-x64 --headless \
     --kernel-args "driver_manager.use_driver_framework_v2=true" \
     --kernel-args "driver_manager.root-driver=fuchsia-boot:///#meta/platform-bus.cm" \
     --kernel-args "devmgr.enable-ephemeral=true"
   ```

   This command starts a headless emulator instance running the Workstation prebuilt image.

   When the instance is up and running, the command prints output similar to
   the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx emu start workstation_eng.qemu-x64 --headless \
     --kernel-args "driver_manager.use_driver_framework_v2=true" \
     --kernel-args "driver_manager.root-driver=fuchsia-boot:///#meta/platform-bus.cm" \
     --kernel-args "devmgr.enable-ephemeral=true"
   Creating SSH key pair: /home/alice/.ssh/fuchsia_ed25519
   Writing authorized_keys file: /home/alice/.ssh/fuchsia_authorized_keys
   Logging to "/home/alice/.local/share/Fuchsia/ffx/emu/instances/fuchsia-emulator/emulator.log"
   Waiting for Fuchsia to start (up to 60 seconds).
   Emulator is ready.
   ```

1. Verify that the new emulator instance is running:

   ```posix-terminal
   tools/ffx emu list
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx emu list
   [Active]  fuchsia-emulator
   ```

1. Set the default target device:

   ```posix-terminal
   tools/ffx target default set fuchsia-emulator
   ```

   This command exits silently without output.

1. Start the Fuchsia package server:

   ```posix-terminal
   tools/ffx repository server start
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx repository server start
   ffx repository server is listening on [::]:8083
   ```

1. Check the list of Fuchsia package repositories available on
   your host machine:

   ```posix-terminal
   tools/ffx repository list
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx repository list
   +-----------------------+------+-------------------------------------------------------------------------------------------------+
   | NAME                  | TYPE | EXTRA                                                                                           |
   +=======================+======+=================================================================================================+
   | workstation-packages* | pm   | /home/alice/.local/share/Fuchsia/ffx/pbms/4751486831982119909/workstation_eng.qemu-x64/packages |
   +-----------------------+------+-------------------------------------------------------------------------------------------------+
   ```

   Notice a package repository (`workstation-packages`) is created
   for the Workstation prebuilt image.

1. Register the `workstation-packages` package repository to the target device:

   ```posix-terminal
   tools/ffx target repository register -r workstation-packages --alias fuchsia.com --alias chromium.org
   ```

   This command exits silently without output.

## 4. Build and load the sample driver {:#build-and-load-the-sample-driver}

The Fuchsia emulator (launched in the [Start the emulator](#start-the-emulator)
section above) is configured to create a virtual device named
[`edu`][edu-device], which  is an educational device for writing drivers.
In the previous section, when the emulator started, Fuchsia’s driver framework
detected this `edu` device in the system, but it wasn’t able to find a driver
that could serve the `edu` device. So the `edu` device was left unmatched.

In this section, we build and publish the [`qemu_edu`][qemu-edu] sample driver
(which is a Fuchsia component). Upon detecting a new driver, the driver
framework will discover that this new `qemu_edu` driver is a match for
the `edu` device. Once matched, the `qemu_edu` driver starts providing the `edu`
device’s services (capabilities) to other components in the system – one of the
services provided by the `edu` device is that it computes a factorial given
an integer.

The tasks include:

*   View the drivers that are currently loaded in the emulator instance.
*   Build and publish the `qemu_edu` driver component.
*   Verify that the `qemu_edu` driver is loaded to the emulator instance.
*   View detailed information on the `qemu_edu` component.

Do the following:

1. View the list of the currently loaded drivers:

   ```posix-terminal
   tools/ffx driver list --loaded
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx driver list --loaded
   fuchsia-boot:///#meta/block.core.cm
   fuchsia-boot:///#meta/bus-pci.cm
   fuchsia-boot:///#meta/display.cm
   fuchsia-boot:///#meta/fvm.cm
   fuchsia-boot:///#meta/goldfish-display.cm
   fuchsia-boot:///#meta/goldfish.cm
   fuchsia-boot:///#meta/goldfish_address_space.cm
   fuchsia-boot:///#meta/goldfish_control.cm
   fuchsia-boot:///#meta/goldfish_sensor.cm
   fuchsia-boot:///#meta/goldfish_sync.cm
   fuchsia-boot:///#meta/hid-input-report.cm
   fuchsia-boot:///#meta/hid.cm
   fuchsia-boot:///#meta/intel-hda.cm
   fuchsia-boot:///#meta/intel-rtc.cm
   fuchsia-boot:///#meta/netdevice-migration.cm
   fuchsia-boot:///#meta/network-device.cm
   fuchsia-boot:///#meta/pc-ps2.cm
   fuchsia-boot:///#meta/platform-bus-x86.cm
   fuchsia-boot:///#meta/platform-bus.cm
   fuchsia-boot:///#meta/qemu-audio-codec.cm
   fuchsia-boot:///#meta/ramdisk.cm
   fuchsia-boot:///#meta/sysmem.cm
   fuchsia-boot:///#meta/virtio_block.cm
   fuchsia-boot:///#meta/virtio_ethernet.cm
   fuchsia-boot:///#meta/virtio_input.cm
   fuchsia-pkg://fuchsia.com/virtual_audio#meta/virtual_audio_driver.cm
   fuchsia-boot:///#meta/ahci.cm
   ```

2. Build and publish the `qemu_edu` driver component:

   ```posix-terminal
   tools/bazel run --config=fuchsia_x64 //src/qemu_edu/drivers:pkg.component
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/bazel run --config=fuchsia_x64 //src/qemu_edu/drivers:pkg.component
   INFO: Analyzed target //src/qemu_edu/drivers:pkg.component (9 packages loaded, 530 targets configured).
   INFO: Found 1 target...
   Target //src/qemu_edu/drivers:pkg.component up-to-date:
     bazel-bin/src/qemu_edu/drivers/pkg.component_run_component.sh
   INFO: Elapsed time: 101.722s, Critical Path: 65.99s
   INFO: 1014 processes: 616 internal, 397 linux-sandbox, 1 local.
   INFO: Build completed successfully, 1014 total actions
   INFO: Build completed successfully, 1014 total actions
   added repository bazel.pkg.component
   Registering fuchsia-pkg://bazel.pkg.component/qemu_edu#meta/qemu_edu.cm
   Successfully bound:
   Node 'root.sys.platform.pt.PCI0.bus.00_06_0_.pci-00_06.0-fidl', Driver 'fuchsia-pkg://bazel.pkg.component/qemu_edu#meta/qemu_edu.cm'.
   ```

3. Verify that the `qemu_edu` driver is now loaded to the Fuchsia emulator
   instance:

   ```posix-terminal
   tools/ffx driver list --loaded
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx driver list --loaded
   fuchsia-boot:///#meta/bus-pci.cm
   fuchsia-boot:///#meta/display.cm
   fuchsia-boot:///#meta/fvm.cm
   fuchsia-boot:///#meta/goldfish-display.cm
   fuchsia-boot:///#meta/goldfish.cm
   fuchsia-boot:///#meta/goldfish_address_space.cm
   fuchsia-boot:///#meta/goldfish_control.cm
   fuchsia-boot:///#meta/goldfish_sensor.cm
   fuchsia-boot:///#meta/goldfish_sync.cm
   fuchsia-boot:///#meta/hid-input-report.cm
   fuchsia-boot:///#meta/hid.cm
   fuchsia-boot:///#meta/intel-hda.cm
   fuchsia-boot:///#meta/intel-rtc.cm
   fuchsia-boot:///#meta/netdevice-migration.cm
   fuchsia-boot:///#meta/network-device.cm
   fuchsia-boot:///#meta/pc-ps2.cm
   fuchsia-boot:///#meta/platform-bus-x86.cm
   fuchsia-boot:///#meta/platform-bus.cm
   fuchsia-boot:///#meta/qemu-audio-codec.cm
   fuchsia-boot:///#meta/ramdisk.cm
   fuchsia-boot:///#meta/sysmem.cm
   fuchsia-boot:///#meta/virtio_block.cm
   fuchsia-boot:///#meta/virtio_ethernet.cm
   fuchsia-boot:///#meta/virtio_input.cm
   fuchsia-pkg://fuchsia.com/virtual_audio#meta/virtual_audio_driver.cm
   {{ '<strong>' }}fuchsia-pkg://bazel.pkg.component/qemu_edu#meta/qemu_edu.cm{{ '</strong>' }}
   fuchsia-boot:///#meta/ahci.cm
   fuchsia-boot:///#meta/block.core.cm
   ```

   Notice that the `qemu_edu` driver is shown in the loaded drivers list.

4. View the `qemu_edu` component information:

   ```posix-terminal
   tools/ffx component show qemu_edu.cm
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx component show qemu_edu.cm
                  Moniker:  /bootstrap/universe-pkg-drivers:root.sys.platform.pt.PCI0.bus.00_06_0_.pci-00_06.0-fidl
                      URL:  fuchsia-pkg://bazel.pkg.component/qemu_edu#meta/qemu_edu.cm
              Instance ID:  None
                     Type:  CML Component
          Component State:  Resolved
    Incoming Capabilities:  /svc/fuchsia.device.fs.Exporter
                            /svc/fuchsia.driver.compat.Service
                            /svc/fuchsia.logger.LogSink
     Exposed Capabilities:  examples.qemuedu.Service
              Merkle root:  619c1b8bd5596e2f705eb56b016b89eb68add91f4a406b6d60d1a8d2690b406d
          Execution State:  Running
             Start reason:  Instance is in a single_run collection
    Outgoing Capabilities:  examples.qemuedu.Service
   ```

5. View the device logs of the `qemu-edu` driver:

   ```posix-terminal
   tools/ffx log --tags qemu-edu dump
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx log --tags qemu-edu dump
   2022-10-27 21:19:30.189][<ffx>]: logger started.
   [184.040][universe-pkg-drivers:root.sys.platform.pt.PCI0.bus.00_06_0_.pci-00_06.0-fidl][qemu-edu,driver][I]: [src/qemu_edu/drivers/qemu_edu.cc:65] edu device version major=1 minor=0
   [184.073][universe-pkg-drivers:root.sys.platform.pt.PCI0.bus.00_06_0_.pci-00_06.0-fidl][qemu-edu,driver][I]: [src/qemu_edu/drivers/qemu_edu.cc:117] Exported devfs_path=sys/platform/pt/PCI0/bus/00:06.0_/qemu-edu service_path=examples.qemuedu.Service/default/device
   ```

## 5. Build and run a tool {:#build-and-run-a-tool}

The `qemu_edu` driver sample includes [tools][eductl_tools] for interacting with the
`qemu_edu` driver. Developers often include binary executables in a Fuchsia package and
run those executables as a component for testing and debugging drivers running in a
Fuchsia system.

In this driver sample, an executable named `eductl_tool` provides two options: `live` and
`fact`. The `live` command checks for the liveness of the `qemu_edu` driver in the system.
The `fact` command takes an integer as an additional argument. The value of the integer is
passed to the `qemu_edu` driver to be used as input for computing the factorial. The
driver computes the factorial and returns the result to the `fact` command, which then
prints the result on the terminal.

The tasks include:

*   Build and run `eductl_tool`.
*   Verify that this tool can interact with the `qemu_edu` driver.

Do the following:

1. Build and run `eductl_tool` (and run the `live` command):

   ```posix-terminal
   tools/bazel run --config=fuchsia_x64 //src/qemu_edu/tools:pkg.eductl_tool -- live
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/bazel run --config=fuchsia_x64 //src/qemu_edu/tools:pkg.eductl_tool -- live
   INFO: Analyzed target //src/qemu_edu/tools:pkg.eductl_tool (1 packages loaded, 15 targets configured).
   INFO: Found 1 target...
   Target //src/qemu_edu/tools:pkg.eductl_tool up-to-date:
     bazel-bin/src/qemu_edu/tools/pkg.eductl_tool_run_driver_tool.sh
   INFO: Elapsed time: 2.406s, Critical Path: 1.84s
   INFO: 22 processes: 7 internal, 14 linux-sandbox, 1 local.
   INFO: Build completed successfully, 22 total actions
   INFO: Build completed successfully, 22 total actions
   added repository bazel.pkg.eductl.tool
   {{ '<strong>' }}Liveness check passed!{{ '</strong>' }}
   ```

   Verify that the line `Liveness check passed!` is printed in the end.

1. Run `eductl_tool` using `fact` and `12` as input:

   ```posix-terminal
   tools/bazel run --config=fuchsia_x64 //src/qemu_edu/tools:pkg.eductl_tool -- fact 12
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/bazel run --config=fuchsia_x64 //src/qemu_edu/tools:pkg.eductl_tool -- fact 12
   ...
   INFO: Build completed successfully, 1 total action
   added repository bazel.pkg.eductl.tool
   {{ '<strong>' }}Factorial(12) = 479001600{{ '</strong>' }}
   ```

   The last line shows that the driver replied `479001600` as the result of the factorial
   to `eductl_tool`, which passed 12 as input to the driver.

1. View the device logs of the `qemu-edu` driver:

   ```posix-terminal
   tools/ffx log --tags qemu-edu dump
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx log --tags qemu-edu dump
   [2022-10-27 21:19:30.189][<ffx>]: logger started.
   [184.040][universe-pkg-drivers:root.sys.platform.pt.PCI0.bus.00_06_0_.pci-00_06.0-fidl][qemu-edu,driver][I]: [src/qemu_edu/drivers/qemu_edu.cc:65] edu device version major=1 minor=0
   [184.073][universe-pkg-drivers:root.sys.platform.pt.PCI0.bus.00_06_0_.pci-00_06.0-fidl][qemu-edu,driver][I]: [src/qemu_edu/drivers/qemu_edu.cc:117] Exported devfs_path=sys/platform/pt/PCI0/bus/00:06.0_/qemu-edu service_path=examples.qemuedu.Service/default/device
   [248.087][universe-pkg-drivers:root.sys.platform.pt.PCI0.bus.00_06_0_.pci-00_06.0-fidl][qemu-edu,driver][I]: [src/qemu_edu/drivers/edu_server.cc:59] Replying with result=true
   [255.504][universe-pkg-drivers:root.sys.platform.pt.PCI0.bus.00_06_0_.pci-00_06.0-fidl][qemu-edu,driver][I]: [src/qemu_edu/drivers/edu_device.cc:124] Replying with factorial=479001600
   ```

   Notice that more messages are now logged from the `qemu-edu` driver.

## 6. Debug the sample driver {:#debug-the-sample-driver}

Use the Fuchsia debugger ([`zxdb`][zxdb-user-guide]) to step through the
sample driver’s code as the driver is running on the emulator instance.

The tasks include:

*   Identify the driver host (which is a component) that is running the
    `qemu_edu` driver.
*   Start the Fuchsia debugger and connect it to the emulator instance.
*   Attach the debugger to the driver host.
*   Set a breakpoint on the driver’s code.
*   Run `eductl_tool`, which triggers the driver to execute its
    instructions.
*   Step through the driver’s code.

Do the following:

1. View the list of the running driver hosts:

   ```posix-terminal
   tools/ffx driver list-hosts
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx driver list-hosts
   Driver Host: 5460
       fuchsia-boot:///#meta/bus-pci.cm
       fuchsia-boot:///#meta/display.cm
       fuchsia-boot:///#meta/goldfish-display.cm
       fuchsia-boot:///#meta/goldfish.cm
       fuchsia-boot:///#meta/goldfish_control.cm
       fuchsia-boot:///#meta/goldfish_sensor.cm
       fuchsia-boot:///#meta/goldfish_sync.cm
       fuchsia-boot:///#meta/hid.cm
       fuchsia-boot:///#meta/platform-bus-x86.cm
       fuchsia-boot:///#meta/platform-bus.cm
       fuchsia-boot:///#meta/sysmem.cm
       fuchsia-pkg://fuchsia.com/virtual_audio#meta/virtual_audio_driver.cm

   Driver Host: 6757
       fuchsia-boot:///#meta/ramdisk.cm

   Driver Host: 9571
       fuchsia-boot:///#meta/intel-rtc.cm

   Driver Host: 9764
       fuchsia-boot:///#meta/pc-ps2.cm

   Driver Host: 9864
       fuchsia-boot:///#meta/hid-input-report.cm
       fuchsia-boot:///#meta/hid.cm
       fuchsia-boot:///#meta/virtio_input.cm

   Driver Host: 9988
       fuchsia-boot:///#meta/intel-hda.cm
       fuchsia-boot:///#meta/qemu-audio-codec.cm

   Driver Host: 10224
       fuchsia-boot:///#meta/goldfish_address_space.cm

   Driver Host: 10412
       fuchsia-boot:///#meta/block.core.cm
       fuchsia-boot:///#meta/fvm.cm
       fuchsia-boot:///#meta/virtio_block.cm

   Driver Host: 10539
       fuchsia-boot:///#meta/ahci.cm

   Driver Host: 10648
       fuchsia-boot:///#meta/netdevice-migration.cm
       fuchsia-boot:///#meta/network-device.cm
       fuchsia-boot:///#meta/virtio_ethernet.cm

   Driver Host: 10802
       fuchsia-boot:///#meta/hid-input-report.cm
       fuchsia-boot:///#meta/hid.cm
       fuchsia-boot:///#meta/virtio_input.cm

   Driver Host: 134104
       fuchsia-pkg://bazel.pkg.component/qemu_edu#meta/qemu_edu.cm
   ```

   Make a note of the PID of the `qemu_edu` driver host (`134104` in the
   example above).

1. Start the Fuchsia debugger:

   ```posix-terminal
   tools/ffx debug connect
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx debug connect
   Connecting (use "disconnect" to cancel)...
   Connected successfully.
   👉 To get started, try "status" or "help".
   [zxdb]
   ```

1. Attach the debugger to the `qemu_edu` driver host:

   <pre class="devsite-click-to-copy">
   <span class="no-select">[zxdb] </span>attach <var>PID</var>
   </pre>

   Replace `PID` with the PID of the `qemu_edu` driver host identified
   in Step 1, for example:

   ```none {:.devsite-disable-click-to-copy}
   [zxdb] attach 134104
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   [zxdb] attach 134104
   Attached Process 1 state=Running koid=134104 name=driver_host2.cm component=driver_host2.cm
   Downloading symbols...
   Symbol downloading complete. 2 succeeded, 0 failed.
   [zxdb]
   ```

1. Set a breakpoint at the driver’s `HandleIrq` function:

   <pre class="devsite-click-to-copy">
   <span class="no-select">[zxdb] </span>break QemuEduDevice::HandleIrq
   </pre>

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   [zxdb] break QemuEduDevice::HandleIrq
   Created Breakpoint 1 @ QemuEduDevice::HandleIrq
      94 void QemuEduDevice::HandleIrq(async_dispatcher_t* dispatcher, async::IrqBase* irq,
    {{ '<strong>' }}◉ 95                               zx_status_t status, const zx_packet_interrupt_t* interrupt) { {{ '</strong>' }}
      96   irq_.ack();
   [zxdb]
   ```

1. In different terminal, run `eductl_tool` (using `fact` and `12` as input)
   to interact with the driver:

   Note:  In this new terminal, make sure that you change to the same work
   directory (for instance, `cd $HOME/fuchsia-drivers`).

   ```posix-terminal
   tools/bazel run --config=fuchsia_x64 //src/qemu_edu/tools:pkg.eductl_tool -- fact 12
   ```

   Unlike in the previous section, after printing output similar to the following,
   the command now waits:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/bazel run --config=fuchsia_x64 //src/qemu_edu/tools:pkg.eductl_tool -- fact 12
   INFO: Analyzed target //src/qemu_edu/tools:pkg.eductl_tool (0 packages loaded, 0 targets configured).
   INFO: Found 1 target...
   Target //src/qemu_edu/tools:pkg.eductl_tool up-to-date:
     bazel-bin/src/qemu_edu/tools/pkg.eductl_tool_run_driver_tool.sh
   INFO: Elapsed time: 0.348s, Critical Path: 0.01s
   INFO: 1 process: 1 internal.
   INFO: Build completed successfully, 1 total action
   INFO: Build completed successfully, 1 total action
   added repository bazel.pkg.eductl.tool
   ```

   In the `zxdb` terminal, verify that the debugger is stopped at the
   `HandleIrq` function, for example:

   ```none {:.devsite-disable-click-to-copy}
   🛑 thread 2 on bp 1 edu_device::QemuEduDevice::HandleIrq(edu_device::QemuEduDevice*, async_dispatcher_t*, async::IrqBase*, zx_status_t, zx_packet_interrupt_t const*) • edu_device.cc:95
      93 // Respond to INTx interrupts triggered by the device, and return the compute result.
      94 void QemuEduDevice::HandleIrq(async_dispatcher_t* dispatcher, async::IrqBase* irq,
    {{ '<strong>' }}▶ 95                               zx_status_t status, const zx_packet_interrupt_t* interrupt) { {{ '</strong>' }}
      96   irq_.ack();
      97   if (!pending_callback_.has_value()) {
   [zxdb]
   ```

1. In the `zxdb` terminal, view the source code around the current breakpoint:

   <pre class="devsite-click-to-copy">
   <span class="no-select">[zxdb] </span>list
   </pre>

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   [zxdb] list
       90   pending_callback_ = std::move(callback);
       91 }
       92
       93 // Respond to INTx interrupts triggered by the device, and return the compute result.
       94 void QemuEduDevice::HandleIrq(async_dispatcher_t* dispatcher, async::IrqBase* irq,
    {{ '<strong>' }}▶  95                               zx_status_t status, const zx_packet_interrupt_t* interrupt) { {{ '</strong>' }}
       96   irq_.ack();
       97   if (!pending_callback_.has_value()) {
       98     FDF_LOG(ERROR, "Received unexpected interrupt!");
       99     return;
      100   }
      101   auto callback = std::move(*pending_callback_);
      102   pending_callback_ = std::nullopt;
      103   if (status != ZX_OK) {
      104     FDF_SLOG(ERROR, "Failed to wait for interrupt", KV("status", zx_status_get_string(status)));
      105     callback(zx::error(status));
   [zxdb]
   ```

1. In the `zxdb` terminal, step through the `HandleIrq` function
   using the `next` command until the value of `factorial` is computed and
   the callback is invoked (that is, until the line 126 is reached):

   <pre class="devsite-click-to-copy">
   <span class="no-select">[zxdb] </span>next
   </pre>

   The last `next` command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   ...
   [zxdb] next
   🛑 thread 2 edu_device::QemuEduDevice::HandleIrq(edu_device::QemuEduDevice*, async_dispatcher_t*, async::IrqBase*, zx_status_t, zx_packet_interrupt_t const*) • edu_device.cc:126
      124   FDF_SLOG(INFO, "Replying with", KV("factorial", factorial));
      125   callback(zx::ok(factorial));
    {{ '<strong>' }}▶ 126 } {{ '</strong>' }}
      127 // [END compute_factorial]
      128
   [zxdb]
   ```

   In the other terminal, after the `HandleIrq` function invokes the callback,
   verify that `eductl_tool` prints the factorial result and exits:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/bazel run --config=fuchsia_x64 //src/qemu_edu/tools:pkg.eductl_tool -- fact 12
   ...
   INFO: Build completed successfully, 1 total action
   added repository bazel.pkg.eductl.tool
   {{ '<strong>' }}Factorial(12) = 479001600{{ '</strong>' }}
   $
   ```

1. In the `zxdb` terminal, type `exit` or press `Ctrl-D` to exit the debugger.

   Note: For more information on usages and best practices on `zxdb`, see the
   [zxdb user guide][zxdb-user-guide].

## 7. Modify and reload the sample driver {:#modify-and-reload-the-sample-driver}

Update the source code of the sample driver and reload it to the emulator
instance.

The tasks include:

*   Restart the emulator instance to unload the `qemu_edu` driver.
*   Update the source code of the `qemu_edu` driver.
*   Load the updated driver.
*   Run `eductl_tool` to verify the change.

Do the following:

1. Stop the emulator instance:

   ```posix-terminal
   tools/ffx emu stop
   ```

   This command stops the currently running emulator instance.

1. Start a new instance of the Fuchsia emulator:

   ```posix-terminal
   tools/ffx emu start workstation_eng.qemu-x64 --headless \
     --kernel-args "driver_manager.use_driver_framework_v2=true" \
     --kernel-args "driver_manager.root-driver=fuchsia-boot:///#meta/platform-bus.cm" \
     --kernel-args "devmgr.enable-ephemeral=true"
   ```

   This command starts a headless emulator instance running the Workstation
   prebuilt image.

1. Use a text editor to open the `edu_device.cc` file of the sample driver, for example:

   ```posix-terminal
   nano src/qemu_edu/drivers/edu_device.cc
   ```

1. In the `QemuEduDevice::HandleIrq` function,
   between the line `uint32_t factorial = mmio_->Read32(kFactorialComputationOffset);`
   (Line 123) and the line `FDF_SLOG(INFO, "Replying with", KV("factorial", factorial));`
   (Line 124), add the following line:

   ```
   factorial=12345;
   ```

   The function should look like below:

   ```none {:.devsite-disable-click-to-copy}
   void QemuEduDevice::HandleIrq(async_dispatcher_t* dispatcher, async::IrqBase* irq,
                                 zx_status_t status, const zx_packet_interrupt_t* interrupt) {
     irq_.ack();
     if (!pending_callback_.has_value()) {
       FDF_LOG(ERROR, "Received unexpected interrupt!");
       return;
     }
     auto callback = std::move(*pending_callback_);
     pending_callback_ = std::nullopt;
     if (status != ZX_OK) {
       FDF_SLOG(ERROR, "Failed to wait for interrupt", KV("status", zx_status_get_string(status)));
       callback(zx::error(status));
       return;
     }

     // Acknowledge the interrupt with the edu device.
     auto int_status = mmio_->Read32(kInterruptStatusRegisterOffset);
     mmio_->Write32(int_status, kInterruptAcknowledgeRegisterOffset);

     // Deassert the legacy INTx interrupt on the PCI bus.
     auto irq_result = pci_->AckInterrupt();
     if (!irq_result.ok() || irq_result->is_error()) {
       FDF_SLOG(ERROR, "Failed to ack PCI interrupt",
                KV("status", irq_result.ok() ? irq_result->error_value() : irq_result.status()));
       callback(zx::error(ZX_ERR_IO));
       return;
     }

     // Reply with the result.
     uint32_t factorial = mmio_->Read32(kFactorialComputationOffset);
     {{ '<strong>' }}factorial=12345;{{ '</strong>' }}
     FDF_SLOG(INFO, "Replying with", KV("factorial", factorial));
     callback(zx::ok(factorial));
   }
   ```

   The function is now updated to always return the value of `12345`.

1. Save the file and close the text editor.

1. Rebuild and run the modified sample driver:

   ```posix-terminal
   tools/bazel run --config=fuchsia_x64 //src/qemu_edu/drivers:pkg.component
   ```

1. Run `eductl_tool` using `fact` and `12` as input:

   ```posix-terminal
   tools/bazel run --config=fuchsia_x64 //src/qemu_edu/tools:pkg.eductl_tool -- fact 12
   ```

   This command now prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/bazel run --config=fuchsia_x64 //src/qemu_edu/tools:pkg.eductl_tool -- fact 12
   ...
   INFO: Build completed successfully, 1 total action
   added repository bazel.pkg.eductl.tool
   {{ '<strong>' }}Factorial(12) = 12345{{ '</strong>' }}
   ```

   The last line shows that the `qemu_edu` driver replied with the
   hardcoded value of `12345` to `eductl_tool`.

**Congratulations! You’re now all set with the Fuchsia driver development!**

## Next steps {:#next-steps}

Learn more about how the `qemu_edu` driver works
in [Codelab: QEMU edu driver][codelab-qemu-edu-driver].

## Appendices

### Clean up the environment {:#clean-up-the-environment}

If you run into a problem while following this guide and decide to start over
from the beginning, consider running the commands below to clean up
your development environment (that is, to clean up directories, build artifacts,
downloaded files, symlinks, configuration settings, and more).

Remove the package repositories created in this guide:

```posix-terminal
tools/ffx repository remove workstation-packages
```

```posix-terminal
tools/ffx repository server stop
```

Remove all existing configurations and data of `ffx`:

* {Linux}

  ```posix-terminal
  tools/ffx daemon stop
  ```

  ```posix-terminal
  rm -rf $HOME/.local/share/Fuchsia/ffx
  ```

* {macOS}

  ```posix-terminal
  tools/ffx daemon stop
  ```

  ```posix-terminal
  rm -rf $HOME/Library/Caches/Fuchsia/ffx
  ```

  ```posix-terminal
  rm -fr $HOME/Library/Fuchsia/ffx
  ```

When Bazel fails to build, try the commands below:


* {Linux}

  Note: Running `bazel clean` or deleting the `$HOME/.cache/bazel` directory
  deletes artifacts downloaded by Bazel, which can be around 4 GB. This means
  Bazel will need to download dependencies again next time you run `bazel build`.

  ```posix-terminal
  tools/bazel clean --expunge
  ```

  ```posix-terminal
  tools/bazel shutdown && rm -rf $HOME/.cache/bazel
  ```

* {macOS}

  Note: Running `bazel clean` or deleting the `/private/var/tmp/bazel$USER`
  directory deletes artifacts downloaded by Bazel, which can be around 4 GB.
  This means Bazel will need to download dependencies again next time you run
  `bazel build`.

  ```posix-terminal
  tools/bazel clean --expunge
  ```

  ```posix-terminal
  tools/bazel shutdown && rm -rf /private/var/tmp/bazel$USER
  ```

Remove the `fuchsia-drivers` directory and its artifacts:

Caution: If the driver samples repository is cloned to a different location
than `$HOME/fuchsia-drivers`, adjust the directory in the command below.
Be extremely careful with the directory path when you run the `rm -rf
<DIR>` command.

```posix-terminal
rm -rf $HOME/fuchsia-drivers
```

Other clean up commands:

```posix-terminal
killall ffx
```

```posix-terminal
killall pm
```

<!-- Reference links -->

[using-the-sdk]: /docs/development/sdk/index.md
[get-started-sdk]: /docs/get-started/sdk/index.md
[sdk-bug]: https://bugs.fuchsia.dev/p/fuchsia/issues/entry?template=Bazel
[kvm]: https://www.linux-kvm.org/page/Main_Page
[qemu]: https://www.qemu.org/
[bazel]: https://bazel.build/docs
[git]: https://git-scm.com/
[git-install]: https://git-scm.com/book/en/v2/Getting-Started-Installing-Git
[bazel-install]: https://bazel.build/install
[bazelisk-download]: https://github.com/bazelbuild/bazelisk/releases
[fuchsia-ssh-keys]: /docs/development/sdk/ffx/create-ssh-keys-for-devices.md
[ticket-01]: https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=97909
[sdk-driver-sample-repo]: https://fuchsia.googlesource.com/sdk-samples/drivers
[clang]: https://clang.llvm.org/
[fuchsia-idk]: /docs/development/idk/README.md
[edu-device]: https://fuchsia.googlesource.com/third_party/qemu/+/refs/heads/main/docs/specs/edu.txt
[qemu-edu]: https://fuchsia.googlesource.com/sdk-samples/drivers/+/refs/heads/main/src/qemu_edu/
[eductl-cml]: https://fuchsia.googlesource.com/sdk-samples/drivers/+/refs/heads/main/src/qemu_edu/meta/eductl.cml
[zxdb-user-guide]: /docs/development/debugger/README.md
[driver-concepts]: /docs/concepts/drivers/README.md
[codelab-qemu-edu-driver]: /docs/get-started/sdk/learn/driver/introduction.md
[driver-framework]: /docs/concepts/drivers/driver_framework.md
[femu]: /docs/development/sdk/ffx/start-the-fuchsia-emulator.md
[eductl_tools]: https://fuchsia.googlesource.com/sdk-samples/drivers/+/refs/heads/main/src/qemu_edu/tools/
