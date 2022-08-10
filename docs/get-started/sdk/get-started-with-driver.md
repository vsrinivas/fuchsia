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
5. [Build and run a tools component](#build-and-run-a-tools-component).
6. [Debug the sample driver](#debug-the-sample-driver).
7. [Modify and reload the sample driver](#modify-and-reload-the-sample-driver).

Found an issue? Please [let us know][sdk-bug]{:.external}.

## 1. Prerequisites {:#prerequisites}

This guide requires that your host machine meets the following criteria:

*  A Linux machine. (**macOS** is not supported yet.)
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
   git clone https://fuchsia.googlesource.com/sdk-samples/drivers --recurse-submodules
   ```

   This creates a new directory named `drivers`, which clones the content of the
   SDK driver samples repository.

1. Go to the new directory:

   ```posix-terminal
   cd drivers
   ```

1. Run the bootstrap script to install Bazel and other required dependencies:

   ```posix-terminal
   scripts/bootstrap.sh
   ```

1. To verify the Fuchsia SDK environment setup, build the sample drivers:

   ```posix-terminal
   tools/bazel build --config=fuchsia_x64 //src/qemu_edu
   ```

   The first build may take a few minutes to download dependencies, such as
   Bazel build rules, [Clang][clang], and [Fuchsia IDK][fuchsia-idk] (which
   includes the `ffx` tool).

   When finished successfully, it prints output similar to the following in the
   end:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/bazel build --config=fuchsia_x64 //src/qemu_edu
   ...
   INFO: Elapsed time: 170.374s, Critical Path: 33.72s
   INFO: 773 processes: 507 internal, 266 linux-sandbox.
   INFO: Build completed successfully, 773 total actions
   ```

5. To verify that you can use the `ffx` tool in your environment, run the
   following command:

   ```posix-terminal
   tools/ffx version -v
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx version -v
   ffx:
     abi-revision: 0xECDB841C251A8CB9
     api-level: 9
     build-version: 2022-07-28T07:03:08+00:00
     integration-commit-hash: d33b25d3cd0cd961c0eaa3ea398b374de15f1ef3
     integration-commit-time: Thu, 28 Jul 2022 07:03:08 +0000

   daemon:
     abi-revision: 0xECDB841C251A8CB9
     api-level: 9
     build-version: 2022-07-28T07:03:08+00:00
     integration-commit-hash: d33b25d3cd0cd961c0eaa3ea398b374de15f1ef3
     integration-commit-time: Thu, 28 Jul 2022 07:03:08 +0000
   ```

   At this point, you only need to confirm that you can run this `ffx` command
   without any errors.

   Note: To ensure that you’re using the right version of `ffx` (which needs to
   match the version of the SDK), consider updating your `PATH` to include the
   SDK's `tools` directory where `ffx` is located (for instance,
   `export PATH="$PATH:$HOME/drivers/tools"`). However, if you don't
   wish to update your `PATH`, ensure that you specify the relative path to
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
   tools/ffx target repository register -r workstation-packages --alias fuchsia.com
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
   fuchsia-boot:///#meta/fvm.cm
   fuchsia-boot:///#meta/hid.cm
   fuchsia-boot:///#meta/intel-rtc.cm
   fuchsia-boot:///#meta/netdevice-migration.cm
   fuchsia-boot:///#meta/network-device.cm
   fuchsia-boot:///#meta/pc-ps2.cm
   fuchsia-boot:///#meta/platform-bus-x86.cm
   fuchsia-boot:///#meta/platform-bus.cm
   fuchsia-boot:///#meta/ramdisk.cm
   fuchsia-boot:///#meta/sysmem.cm
   fuchsia-boot:///#meta/virtio_block.cm
   fuchsia-boot:///#meta/virtio_ethernet.cm
   fuchsia-pkg://fuchsia.com/virtual_audio#meta/virtual_audio_driver.cm
   ```

2. Build and publish the `qemu_edu` driver component:

   ```posix-terminal
   tools/bazel run --config=fuchsia_x64 //src/qemu_edu:pkg.component
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/bazel run --config=fuchsia_x64 //src/qemu_edu:pkg.component
   INFO: Analyzed target //src/qemu_edu:pkg.component (8 packages loaded, 498 targets configured).
   INFO: Found 1 target...
   Target //src/qemu_edu:pkg.component up-to-date:
     bazel-bin/src/qemu_edu/pkg.component_run_component.sh
   INFO: Elapsed time: 47.995s, Critical Path: 38.38s
   INFO: 791 processes: 516 internal, 274 linux-sandbox, 1 local.
   INFO: Build completed successfully, 791 total actions
   INFO: Build completed successfully, 791 total actions
   added repository bazel.pkg.component
   Registering fuchsia-pkg://bazel.pkg.component/qemu_edu#meta/qemu_edu.cm
   Successfully bound:
   Node 'root.sys.platform.platform-passthrough.PCI0.bus.00_06_0_', Driver 'fuchsia-pkg://bazel.pkg.component/qemu_edu#meta/qemu_edu.cm'.
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
   fuchsia-boot:///#meta/fvm.cm
   fuchsia-boot:///#meta/hid.cm
   fuchsia-boot:///#meta/intel-rtc.cm
   fuchsia-boot:///#meta/netdevice-migration.cm
   fuchsia-boot:///#meta/network-device.cm
   fuchsia-boot:///#meta/pc-ps2.cm
   fuchsia-boot:///#meta/platform-bus-x86.cm
   fuchsia-boot:///#meta/platform-bus.cm
   fuchsia-boot:///#meta/ramdisk.cm
   fuchsia-boot:///#meta/sysmem.cm
   fuchsia-boot:///#meta/virtio_block.cm
   fuchsia-boot:///#meta/virtio_ethernet.cm
   fuchsia-pkg://fuchsia.com/virtual_audio#meta/virtual_audio_driver.cm
   {{ '<strong>' }}fuchsia-pkg://bazel.pkg.component/qemu_edu#meta/qemu_edu.cm{{ '</strong>' }}
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
                  Moniker:  /bootstrap/universe-pkg-drivers:root.sys.platform.platform-passthrough.PCI0.bus.00_06_0_
                      URL:  fuchsia-pkg://bazel.pkg.component/qemu_edu#meta/qemu_edu.cm
              Instance ID:  None
                     Type:  CML Component
          Component State:  Resolved
    Incoming Capabilities:  /svc/fuchsia.device.fs.Exporter
                            /svc/fuchsia.driver.compat.Service
                            /svc/fuchsia.logger.LogSink
     Exposed Capabilities:  fuchsia.hardware.qemuedu.Device
              Merkle root:  957cd123aba43515a5acc8b95a2abfecba3d714655f3a2a4e581c13ca0db6f7d
          Execution State:  Running
             Start reason:  Instance is in a single_run collection
    Outgoing Capabilities:  qemu-edu
   ```

5. View the device logs of the `qemu_edu` driver:

   ```posix-terminal
   tools/ffx log --filter qemu_edu dump
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx log --filter qemu_edu dump
   ...
   [173.618][pkg-resolver][pkg-resolver][I] resolved fuchsia-pkg://bazel.pkg.component/qemu_edu as fuchsia-pkg://bazel.pkg.component/qemu_edu to 957cd123aba43515a5acc8b95a2abfecba3d714655f3a2a4e581c13ca0db6f7d with TUF
   [173.618][pkg-resolver][pkg-resolver][I] get_hash for fuchsia-pkg://bazel.pkg.component/qemu_edu as fuchsia-pkg://bazel.pkg.component/qemu_edu to 957cd123aba43515a5acc8b95a2abfecba3d714655f3a2a4e581c13ca0db6f7d with TUF
   [173.623][driver_index][driver_index,driver][I] Registered driver successfully: fuchsia-pkg://bazel.pkg.component/qemu_edu#meta/qemu_edu.cm.
   [173.653][pkg-resolver][pkg-resolver][I] Fetching blobs for fuchsia-pkg://bazel.pkg.component/qemu_edu: []
   [173.656][pkg-resolver][pkg-resolver][I] resolved fuchsia-pkg://bazel.pkg.component/qemu_edu as fuchsia-pkg://bazel.pkg.component/qemu_edu to 957cd123aba43515a5acc8b95a2abfecba3d714655f3a2a4e581c13ca0db6f7d with TUF
   [173.662][driver_manager][driver_manager.cm][I]: [driver_runner.cc:377] Binding fuchsia-pkg://bazel.pkg.component/qemu_edu#meta/qemu_edu.cm to  00_06_0_
   [173.891][universe-pkg-drivers:root.sys.platform.platform-passthrough.PCI0.bus.00_06_0_][qemu-edu,driver][I]: [src/qemu_edu/qemu_edu.cc:117] edu device version major=1 minor=0
   ```

## 5. Build and run a tools component {:#build-and-run-a-tools-component}

The `qemu_edu` driver sample has a “tools” component named `eductl`, which can
interact with the sample driver. Developers create these tools components for
testing and debugging drivers during development.

In this case, the `eductl` component contacts the `qemu_edu` driver and passes
an integer as input. The driver (using the resource of the `edu` virtual device)
computes the integer's factorial and returns the result to the `eductl`
component. The component then prints the result in the log.

The tasks include:

*   Build and run the `eductl` component.
*   Verify that the component can interact with the `qemu_edu` driver.

Do the following:

1. Build and run the `eductl` component:

   ```posix-terminal
   tools/bazel run --config=fuchsia_x64 //src/qemu_edu:eductl_pkg.eductl_component
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/bazel run --config=fuchsia_x64 //src/qemu_edu:eductl_pkg.eductl_component
   INFO: Analyzed target //src/qemu_edu:eductl_pkg.eductl_component (0 packages loaded, 19 targets configured).
   INFO: Found 1 target...
   Target //src/qemu_edu:eductl_pkg.eductl_component up-to-date:
     bazel-bin/src/qemu_edu/eductl_pkg.eductl_component_run_component.sh
   INFO: Elapsed time: 2.028s, Critical Path: 1.50s
   INFO: 23 processes: 7 internal, 15 linux-sandbox, 1 local.
   INFO: Build completed successfully, 23 total actions
   INFO: Build completed successfully, 23 total actions
   added repository bazel.eductl.pkg.eductl.component
   URL: fuchsia-pkg://bazel.eductl.pkg.eductl.component/eductl#meta/eductl.cm
   Moniker: /core/ffx-laboratory:eductl
   Creating component instance...
   Starting component instance...
   Success! The component instance has been started.
   ```

2. View the device logs:

   ```posix-terminal
   tools/ffx log --filter qemu_edu --filter eductl dump
   ```

   The device logs contain lines similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx log --filter qemu_edu --filter eductl dump
   ...
   [214.156][universe-pkg-drivers:root.sys.platform.platform-passthrough.PCI0.bus.00_06_0_][qemu-edu,driver][I]: [src/qemu_edu/qemu_edu.cc:234] Replying with factorial=479001600
   [214.157][ffx-laboratory:eductl][][I] Factorial(12) = 479001600
   ...
   ```

   These lines show that the driver replied the result of `factorial=479001600`
   to the `eductl` component, which previously passed 12 as input to the driver.
   (For the default input, see this [`eductl.cml`][eductl-cml] file.)

## 6. Debug the sample driver {:#debug-the-sample-driver}

Use the Fuchsia debugger ([`zxdb`][zxdb-user-guide]) to step through the
sample driver’s code as the driver is running on the emulator instance.

The tasks include:

*   Identify the driver host (which is a component) that is running the
    `qemu_edu` driver.
*   Start the Fuchsia debugger and connect it to the emulator instance.
*   Attach the debugger to the driver host.
*   Set a breakpoint on the driver’s code.
*   Run the tools component, which triggers the driver to execute its
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
   Driver Host: 4690
       fuchsia-boot:///#meta/block.core.cm
       fuchsia-boot:///#meta/bus-pci.cm
       fuchsia-boot:///#meta/fvm.cm
       fuchsia-boot:///#meta/hid.cm
       fuchsia-boot:///#meta/netdevice-migration.cm
       fuchsia-boot:///#meta/network-device.cm
       fuchsia-boot:///#meta/platform-bus-x86.cm
       fuchsia-boot:///#meta/platform-bus.cm
       fuchsia-boot:///#meta/ramdisk.cm
       fuchsia-boot:///#meta/sysmem.cm
       fuchsia-boot:///#meta/virtio_block.cm
       fuchsia-boot:///#meta/virtio_ethernet.cm
       fuchsia-boot:///#meta/zxcrypt.cm
       fuchsia-pkg://fuchsia.com/virtual_audio#meta/virtual_audio_driver.cm

   Driver Host: 7820
       fuchsia-boot:///#meta/intel-rtc.cm

   Driver Host: 7903
       fuchsia-boot:///#meta/pc-ps2.cm

   Driver Host: 50125
       fuchsia-pkg://bazel.pkg.component/qemu_edu#meta/qemu_edu.cm
   ```

   Make a note of the PID of the `qemu_edu` driver host (`50125` in the
   example above).

2. Start the Fuchsia debugger:

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

3. Attach the debugger to the `qemu_edu` driver host:

   <pre class="devsite-click-to-copy">
   <span class="no-select">[zxdb] </span>attach <var>PID</var>
   </pre>

   Replace `PID` with the PID of the `qemu_edu` driver host identified
   in Step 1, for example:

   ```none {:.devsite-disable-click-to-copy}
   [zxdb] attach 50125
   ```

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   [zxdb] attach 50125
   Attached Process 1 state=Running koid=50125 name=driver_host2.cm
   Downloading symbols...
   Symbol downloading complete. 7 succeeded, 0 failed.
   [zxdb]
   ```

4. Set a breakpoint at the driver’s `ComputeFactorial` function:

   <pre class="devsite-click-to-copy">
   <span class="no-select">[zxdb] </span>break QemuEduDriver::ComputeFactorial
   </pre>

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   [zxdb] break QemuEduDriver::ComputeFactorial
   Created Breakpoint 1 @ QemuEduDriver::ComputeFactorial
      217 void QemuEduDriver::ComputeFactorial(ComputeFactorialRequestView request,
    {{ '<strong>' }}◉ 218                                      ComputeFactorialCompleter::Sync& completer) { {{ '</strong>' }}
      219   // Write a value into the factorial register.
   [zxdb]
   ```

5. In different terminal, run the tools component:

   Note:  In this new terminal, make sure that you change to the same work
   directory (for instance, `cd $HOME/drivers`).

   ```posix-terminal
   tools/bazel run --config=fuchsia_x64 //src/qemu_edu:eductl_pkg.eductl_component
   ```

   In the `zxdb` terminal, verify that the debugger is stopped at the driver’s
   `ComputeFactorial` function, for example:

   ```none {:.devsite-disable-click-to-copy}
   🛑 thread 2 on bp 1 qemu_edu::QemuEduDriver::ComputeFactorial(qemu_edu::QemuEduDriver*, fidl::WireServer<fuchsia_hardware_qemuedu::Device>::ComputeFactorialRequestView, fidl::Completer<fidl::internal::WireCompleterBase<fuchsia_hardware_qemuedu::Device::ComputeFactorial> >::Sync&) • qemu_edu.cc:218
      216 // Driver Service: Compute factorial on the edu device
      217 void QemuEduDriver::ComputeFactorial(ComputeFactorialRequestView request,
    {{ '<strong>' }}▶ 218                                      ComputeFactorialCompleter::Sync& completer) { {{ '</strong>' }}
      219   // Write a value into the factorial register.
      220   uint32_t input = request->input;
   [zxdb]
   ```

6. In the `zxdb` terminal, view the source code around the current breakpoint:

   <pre class="devsite-click-to-copy">
   <span class="no-select">[zxdb] </span>list
   </pre>

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   [zxdb] list
      213   return zx::ok();
      214 }
      215
      216 // Driver Service: Compute factorial on the edu device
      217 void QemuEduDriver::ComputeFactorial(ComputeFactorialRequestView request,
    {{ '<strong>' }}▶ 218                                      ComputeFactorialCompleter::Sync& completer) { {{ '</strong>' }}
      219   // Write a value into the factorial register.
      220   uint32_t input = request->input;
      221
      222   mmio_->Write32(input, edu_device_registers::kFactorialCompoutationOffset);
      223
      224   // Busy wait on the factorial status bit.
      225   while (true) {
      226     const auto status = edu_device_registers::Status::Get().ReadFrom(&*mmio_);
      227     if (!status.busy())
      228       break;
   [zxdb]
   ```

7. In the `zxdb` terminal, step through the code using the `next`
   command until the value of `factorial` is read from the device (that is,
   until the line 234 is reached):

   <pre class="devsite-click-to-copy">
   <span class="no-select">[zxdb] </span>next
   </pre>

   The last `next` command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   ...
   [zxdb] next
   🛑 thread 2 qemu_edu::QemuEduDriver::ComputeFactorial(qemu_edu::QemuEduDriver*, fidl::WireServer<fuchsia_hardware_qemuedu::Device>::ComputeFactorialRequestView, fidl::Completer<fidl::internal::WireCompleterBase<fuchsia_hardware_qemuedu::Device::ComputeFactorial> >::Sync&) • qemu_edu.cc:234
      232   uint32_t factorial = mmio_->Read32(edu_device_registers::kFactorialCompoutationOffset);
      233
    {{ '<strong>' }}▶ 234   FDF_SLOG(INFO, "Replying with", KV("factorial", factorial));{{ '</strong>' }}
      235   completer.Reply(factorial);
      236 }
   [zxdb]
   ```

8. Print the `factorial` variable:

   <pre class="devsite-click-to-copy">
   <span class="no-select">[zxdb] </span>print factorial
   </pre>

   This command prints output similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   [zxdb] print factorial
   479001600
   [zxdb]
   ```

1. To exit the `zxdb` terminal, type `exit` or press `Ctrl-D`.

   Note: For more information on usages and best practices on `zxdb`, see the
   [zxdb user guide][zxdb-user-guide].

## 7. Modify and reload the sample driver {:#modify-and-reload-the-sample-driver}

Update the source code of the sample driver and reload it to the emulator
instance.

The tasks include:

*   Restart the emulator instance to unload the `qemu_edu` driver.
*   Update the source code of the `qemu_edu` driver.
*   Load the updated driver.
*   Run the tools component to verify the change.

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

1. Use a text editor to open the source code of the sample driver, for example:

   ```posix-terminal
   nano src/qemu_edu/qemu_edu.cc
   ```

1. In the `QemuEduDriver::ComputeFactorial` function,
   between the line
   `uint32_t factorial = mmio_->Read32(regs::kFactorialCompoutationOffset);`
   (Line 232) and the `FDF_SLOG()` call (Line 234), add the following line:

   ```
   factorial=12345;
   ```

   The function should look like below:

   ```none {:.devsite-disable-click-to-copy}
   void QemuEduDriver::ComputeFactorial(ComputeFactorialRequestView request,
                                        ComputeFactorialCompleter::Sync& completer) {
     // Write a value into the factorial register.
     uint32_t input = request->input;

     mmio_->Write32(input, regs::kFactorialCompoutationOffset);

     // Busy wait on the factorial status bit.
     while (true) {
       const auto status = regs::Status::Get().ReadFrom(&*mmio_);
       if (!status.busy())
         break;
     }

     // Return the result.
     uint32_t factorial = mmio_->Read32(regs::kFactorialCompoutationOffset);
     {{ '<strong>' }}factorial = 12345;{{ '</strong>' }}
     FDF_SLOG(INFO, "Replying with", KV("factorial", factorial));
     completer.Reply(factorial);
   }
   ```

   The function is now updated to return the value of `12345` only.

1. Save the file and close the text editor.

1. Rebuild and run the modified sample driver:

   ```posix-terminal
   tools/bazel run --config=fuchsia_x64 //src/qemu_edu:pkg.component
   ```

1. Run the tools component:

   ```posix-terminal
   tools/bazel run --config=fuchsia_x64 //src/qemu_edu:eductl_pkg.eductl_component
   ```

1. To verify that change, view the device logs:

   ```posix-terminal
   tools/ffx log --filter qemu_edu --filter eductl dump
   ```

   The device logs contain lines similar to the following:

   ```none {:.devsite-disable-click-to-copy}
   $ tools/ffx log --filter qemu_edu --filter eductl dump
   ...
   [94.914][universe-pkg-drivers:root.sys.platform.platform-passthrough.PCI0.bus.00_06_0_][qemu-edu,driver][I]: [src/qemu_edu/qemu_edu.cc:234] Replying with factorial=12345
   [94.916][ffx-laboratory:eductl][][I] Factorial(12) = 12345
   ...
   ```

   These lines show that the `qemu_edu` driver replied with the
   hardcoded value of `factorial=12345` to the `eductl` tools component.

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

```posix-terminal
tools/ffx daemon stop
```

```posix-terminal
rm -rf $HOME/.local/share/Fuchsia/ffx
```

When Bazel fails to build, try the commands below:

Caution: Running `bazel clean` or deleting the `$HOME/.cache/bazel` directory
deletes all the artifacts downloaded by Bazel, which can be around 4 GB.
This means Bazel will need to download those dependencies again
the next time you run `bazel build`.

```posix-terminal
tools/bazel clean --expunge
```

```posix-terminal
tools/bazel shutdown && rm -rf $HOME/.cache/bazel
```

Remove the `drivers` directory and its artifacts:

Caution: If the SDK samples repository is cloned to a different location
than `$HOME/drivers`, adjust the directory path in the command below.
Be extremely careful with the directory path when you run the `rm -rf
<DIR>` command.

```posix-terminal
rm -rf $HOME/drivers
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
