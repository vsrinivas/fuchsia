# Test and debug the driver

Fuchsia supports step-through debugging of components using the Fuchsia debugger (`zxdb`).
The debugger attaches to the host process where a component is running, and allows the developer
to set breakpoints and step through code execution. The Test Runner Framework enables developers
to write tests that exercise driver components.

In this section, you'll use the Fuchsia debugger (`zxdb`) to inspect a running driver and build a
test component to exercise the driver's functionality.

Note: For complete details on `zxdb` and the Test Runner Framework, see
[The Fuchsia Debugger][fuchsia-debugger] and [Testing with Components][component-testing].

## Connect the debugger

To connect the Fuchsia debugger to the driver component, you'll need to determine the PID of the
host process. Use the `ffx driver list-hosts` command to discover the PID of the host process where
the driver is loaded:

```posix-terminal
ffx driver list-hosts
```

The command outputs a list similar to the following. Locate the driver host where the `qemu_edu`
driver is listed:

```none {:.devsite-disable-click-to-copy}
Driver Host: 5053
    fuchsia-boot:///#meta/block.core.cm
    fuchsia-boot:///#meta/bus-pci.cm
    fuchsia-boot:///#meta/cpu-trace.cm
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
    fuchsia-pkg://fuchsia.com/virtual_audio#meta/virtual_audio_driver.cm

Driver Host: 7774
    fuchsia-boot:///#meta/intel-rtc.cm

Driver Host: 7855
    fuchsia-boot:///#meta/pc-ps2.cm

{{ '<strong>' }}Driver Host: 44887 {{ '</strong>' }}
{{ '<strong>' }}    fuchsia-pkg://fuchsiasamples.com/qemu_edu#meta/qemu_edu.cm {{ '</strong>' }}
```

Make a note of the PID for the `qemu_edu` driver host. In the above example, the PID is 44887.

Start the Fuchsia debugger with `ffx debug connect`:

```posix-terminal
ffx debug connect
```

Once the debugger connects to the target device, attach to the `qemu_edu` driver host from the
`zxdb` prompt:

```none {:.devsite-disable-click-to-copy}
[zxdb] attach <Host PID>
```

Replace "Host PID" with the PID of the driver host identified in the previous step. For example:

```none {:.devsite-disable-click-to-copy}
[zxdb] attach 44887
```

Set a breakpoint in the driver's `ComputeFactorial` function:

```none {:.devsite-disable-click-to-copy}
[zxdb] break QemuEduDriver::ComputeFactorial
```

The command prints output similar to the following to indicate where the breakpoint is set:

```none {:.devsite-disable-click-to-copy}
[zxdb] break QemuEduDriver::ComputeFactorial
Created Breakpoint 1 @ QemuEduDriver::ComputeFactorial
   215 void QemuEduDriver::ComputeFactorial(ComputeFactorialRequestView request,
 ◉ 216                                      ComputeFactorialCompleter::Sync& completer) {
   217   // Write a value into the factorial register.
```

## Step through the driver function

In a separate terminal, run the `eductl` tools component again:

```posix-terminal
bazel run --config=fuchsia_x64 //fuchsia-codelab/qemu_edu:eductl_pkg.eductl_component
```

In the `zxdb` terminal, verify that the debugger has hit the breakpoint in the driver's
`ComputeFactorial` function. For example:

```none {:.devsite-disable-click-to-copy}
🛑 thread 2 on bp 1 qemu_edu::QemuEduDriver::ComputeFactorial(qemu_edu::QemuEduDriver*, fidl::WireServer<fuchsia_hardware_qemuedu::Device>::ComputeFactorialRequestView, fidl::Completer<fidl::internal::WireCompleterBase<fuchsia_hardware_qemuedu::Device::ComputeFactorial> >::Sync&) • qemu_edu.cc:216
   214 // Driver Service: Compute factorial on the edu device
   215 void QemuEduDriver::ComputeFactorial(ComputeFactorialRequestView request,
 ▶ 216                                      ComputeFactorialCompleter::Sync& completer) {
   217   // Write a value into the factorial register.
   218   uint32_t input = request->input;
```

Use the `list` command at the `zxdb` prompt to show where execution is currently paused:

```none {:.devsite-disable-click-to-copy}
[zxdb] list
   211   return zx::ok();
   212 }
   213
   214 // Driver Service: Compute factorial on the edu device
   215 void QemuEduDriver::ComputeFactorial(ComputeFactorialRequestView request,
 ▶ 216                                      ComputeFactorialCompleter::Sync& completer) {
   217   // Write a value into the factorial register.
   218   uint32_t input = request->input;
   219
   220   mmio_->Write32(input, edu_device_registers::kFactorialCompoutationOffset);
   221
   222   // Busy wait on the factorial status bit.
   223   while (true) {
   224     const auto status = edu_device_registers::Status::Get().ReadFrom(&*mmio_);
   225     if (!status.busy())
   226       break;
```

Step into the `ComputeFactorial` function using the `next` command:

```none {:.devsite-disable-click-to-copy}
[zxdb] next
```

Print the contents of the request passed into the function, containing the factorial input value:

```none {:.devsite-disable-click-to-copy}
[zxdb] print request
{request_ = (*)0x24302c4be78 ➔ {fuchsia_hardware_qemuedu:… = {input = 12}}}
```

Exit the debugger session and disconnect:

```none {:.devsite-disable-click-to-copy}
[zxdb] exit
```

## Create a new system test component

In this section, you'll create a new test component that exercises the exposed functions of the
`qemu_edu` driver.

After this section is complete, the project should have the following directory structure:

```none {:.devsite-disable-click-to-copy}
//fuchsia-codelab/qemu_edu
                  |- BUILD.bazel
                  |- meta
                  |   |- eductl.cml
{{ '<strong>' }}                  |   |- qemu_edu_system_test.cml {{ '</strong>' }}
                  |   |- qemu_edu.cml
                  |- eductl.cc
{{ '<strong>' }}                  |- qemu_edu_system_test.cc {{ '</strong>' }}
                  |- qemu_edu.bind
                  |- qemu_edu.cc
                  |- qemu_edu.fidl
                  |- qemu_edu.h
                  |- registers.h
```

Create a new `qemu_edu/meta/qemu_edu_system_test.cml` component manifest file to the project
with the following contents:

`qemu_edu/meta/qemu_edu_system_test.cml`:

```json5
{
    include: [
        "syslog/client.shard.cml",
        "sys/testing/elf_test_runner.shard.cml",
    ],
    program: {
        binary: 'bin/qemu_edu_system_test',
    },
    use: [
        {
            directory: "dev",
            rights: [ "rw*" ],
            path: "/dev",
        },
    ],
    // Required to enable access to devfs
    facets: {
        "fuchsia.test": { type: "devices" },
    },
}

```

Similar to `eductl`, the test component discovers and accesses the driver using the `dev`
directory capability. This component also includes the `elf_test_runner.shard.cml`, which enables
it to run using the Test Runner Framework.

Create a new `qemu_edu/qemu_edu_system_test.cc` file with the following contents to implement the
tests:

`qemu_edu/qemu_edu_system_test.cc`:

```cpp
#include <dirent.h>
#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <sys/types.h>
#include <gtest/gtest.h>

#include <fidl/fuchsia.hardware.qemuedu/cpp/wire.h>

namespace {

constexpr char kEduDevicePath[] =
    "/dev/sys/platform/platform-passthrough/PCI0/bus/00:06.0_/qemu-edu";

class QemuEduSystemTest : public testing::Test {
 public:
  void SetUp() {
    int device = open(kEduDevicePath, O_RDWR);
    ASSERT_GE(device, 0);

    fidl::ClientEnd<fuchsia_hardware_qemuedu::Device> client_end;
    ASSERT_EQ(fdio_get_service_handle(device, client_end.channel().reset_and_get_address()), ZX_OK);
    device_ = fidl::BindSyncClient(std::move(client_end));
  }

  fidl::WireSyncClient<fuchsia_hardware_qemuedu::Device>& device() { return device_; }

 private:
  fidl::WireSyncClient<fuchsia_hardware_qemuedu::Device> device_;
};

TEST_F(QemuEduSystemTest, LivenessCheck) {
  fidl::WireResult result = device()->LivenessCheck();
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_TRUE(result->result);
}

TEST_F(QemuEduSystemTest, ComputeFactorial) {
  std::array<uint32_t, 11> kExpected = {
      1, 1, 2, 6, 24, 120, 720, 5040, 40320, 362880, 3628800,
  };
  for (int i = 0; i < kExpected.size(); i++) {
    fidl::WireResult result = device()->ComputeFactorial(i);
    ASSERT_EQ(result.status(), ZX_OK);
    EXPECT_EQ(result->output, kExpected[i]);
  }
}

}  // namespace

```

Each test case opens the device driver and exercises one of its exposed functions.

Add the following new rules to the project's build configuration to build the test component into
a Fuchsia test package:

`qemu_edu/BUILD.bazel`:

```bazel
fuchsia_cc_test(
    name = "qemu_edu_system_test",
    size = "small",
    srcs = [
        "qemu_edu_system_test.cc",
    ],
    deps = ["@com_google_googletest//:gtest_main"] + if_fuchsia([
        ":fuchsia.hardware.qemuedu_cc",
        "@fuchsia_sdk//pkg/fdio",
    ]),
)

fuchsia_component_manifest(
    name = "system_test_manifest",
    src = "meta/qemu_edu_system_test.cml",
    includes = [
        "@fuchsia_sdk//pkg/sys/testing:elf_test_runner",
        "@fuchsia_sdk//pkg/syslog:client",
    ],
)

fuchsia_component(
    name = "system_test_component",
    testonly = True,
    manifest = "system_test_manifest",
    deps = [
        ":qemu_edu_system_test",
    ],
)

fuchsia_test_package(
    name = "test_pkg",
    package_name = "qemu_edu_system_test",
    visibility = ["//visibility:public"],
    deps = [
        ":system_test_component",
    ],
)
```

## Run the system test

Use the `bazel run` command to build and execute the test component target:

```posix-terminal
bazel run --config=fuchsia_x64 //fuchsia-codelab/qemu_edu:test_pkg.system_test_component
```

The `bazel run` command performs the following steps:

1.  Build the component and package
1.  Publish the package to a local package repository
1.  Register the package repository with the target device
1.  Use `ffx test run` to execute the component's test suite

Verify that all the tests pass successfully:

```none {:.devsite-disable-click-to-copy}
Running test 'fuchsia-pkg://bazel.test.pkg.system.test.component/qemu_edu_system_test#meta/qemu_edu_system_test.cm'
[RUNNING]	main
[stdout - main]
Running main() from gmock_main.cc
[==========] Running 2 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 2 tests from QemuEduSystemTest
[ RUN      ] QemuEduSystemTest.LivenessCheck
[       OK ] QemuEduSystemTest.LivenessCheck (4 ms)
[ RUN      ] QemuEduSystemTest.ComputeFactorial
[       OK ] QemuEduSystemTest.ComputeFactorial (4 ms)
[----------] 2 tests from QemuEduSystemTest (9 ms total)

[----------] Global test environment tear-down
[==========] 2 tests from 1 test suite ran. (9 ms total)
[  PASSED  ] 2 tests.
[PASSED]	main
```

## What's Next?

Congratulations! You've successfully debugged and added tests to your Fuchsia driver.

Now that you have experienced the basics of developing drivers on Fuchsia, take your knowledge to
the next level and dive deeper with the:

<a class="button button-primary"
    href="/docs/concepts/drivers">Driver concepts</a>

<!-- Reference links -->

[component-testing]: /docs/development/testing/components/README.md
[fuchsia-debugger]: /docs/development/debugger/README.md
