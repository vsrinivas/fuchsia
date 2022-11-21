# adb on Fuchsia

Android Debug Bridge (adb) is a command-line tool that can be used for discovering and communicating
with Fuchsia devices from a development host machine. adb consists of a daemon running on the device
as well as a client and server running on the host. For more details about the design of adb see
[OVERVIEW.TXT](https://android.googlesource.com/platform/packages/modules/adb/+/refs/tags/android-13.0.0_r3/OVERVIEW.TXT).

This directory contains code for Fuchsia device side adb support - adb protocol server, adb services
and necessary USB drivers.

NOTE: The purpose of this tool is mainly to support hardware testing workflows. Please consult the
[OWNERS](OWNERS) before using adb on Fuchsia for additional use cases.

Below is the list of adb commands supported currently.

* [adb devices][adb-devices]
* [adb shell][adb-shell]
* [adb push][adb-push]/[pull][adb-pull]
* Scripting commands like
  * [adb wait-for-device][adb-wait-for]
  * [adb get-state][adb-get-state]/[get-serialno][adb-get-serialno]/[get-devpath][adb-get-devpath]
* Debugging commands like
  * [adb start-server][adb-start-server]/[kill-server][adb-kill-server]
  * [adb reconnect][adb-reconnect] ([adb reconnect device][adb-reconnect-device], [adb reconnect
    offline][adb-reconnect-offline] variants are not supported)

Note that these commands provide functionality similar to that of adb on Android but do not try to
match the behavior. These commands are backed by Fuchsia concepts like components, packages,
capabilities etc. and are closer to `ffx` counterparts in terms of the behavior.

## How to include adb in a Fuchsia image

adb support is not included by default in any of the products. To manually include it, add these
labels to `fx set`

```GN
--args='dev_kernel_cmdline=["driver.usb.peripheral=adb"]'\
--args='dev_bootfs_labels=["//src/developer/adb:drivers"]'\
--with-base '//src/developer/adb:adb'\
--args='core_realm_shards+=["//src/developer/adb:core_shards"]'
```

Alternatively, you can use `driver.usb.peripheral=cdc_adb`, to have both networking and adb
interfaces enabled simultaneously.

adb can only be used on boards that support USB peripheral mode.

## Usage

The general usage of the adb command line tool is described in the [Android
docs](https://developer.android.com/studio/command-line/adb). Specific differences in Fuchsia are
described in this section.

To get started with adb on Fuchsia -

* Make sure that the Fuchsia device is running an image that is built with adb support as mentioned
  in the previous section.
* Connect the device to the development host using a USB cable. Optionally, you can ensure that adb
  interface is enabled by using tools like `lsusb` (on Linux hosts) or equivalent tools on other
  platforms. Example output of `lsusb` -

  ```sh
  $ lsusb
  Bus 001 Device 073: ID 18d1:a025 Google Inc. ADB

  or

  Bus 001 Device 073: ID 18d1:a026 Google Inc. CDC Ethernet & ADB
  ```

* Download the stock adb client from [Android
  SDK](https://developer.android.com/studio/command-line/adb) onto the development host or use
  prebuilt adb client from Fuchsia tree
  [//prebuilt/starnix/internal/android-image-amd64/adb](//prebuilt/starnix/internal/android-image-amd64/adb).

### Using adb shell

`adb shell` is built to share the same backend as `ffx component explore`. By default, it is
configured to use `/bootstrap/console-launcher` moniker, meaning the `adb shell` will have same
capabilities as serial console. You can update the structured configuration in
[//src/developer/adb/bin/adb-shell/BUILD.gn](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/developer/adb/bin/adb-shell/BUILD.gn;l=49)
to change the moniker at build time. All of the Fuchsia CLI tools that can run on the serial console
should work in the same fashion on `adb shell`.

### Using adb file transfer

adb file transfer would allow you to push/pull files from a Fuchsia device. This shares the same
backend API as `ffx component storage`. Similar to `ffx component storage`, you need to be aware of
the moniker of the component you wish to push/pull a file. Alternatively, you can set the default
moniker in the component args in
[//src/developer/adb/bin/adb-file-sync/meta/adb-file-sync.cml](https://cs.opensource.google/fuchsia/fuchsia/+/master:src/developer/adb/bin/adb-file-sync/meta/adb-file-sync.cml;l=9).
Example commands -

```sh
adb push ~/tmp.txt /core/exceptions::/tmp
adb pull /core/exceptions::/tmp/tmp.txt ~/foo.txt
```

[adb-devices]: https://android.googlesource.com/platform/packages/modules/adb/+/refs/tags/android-13.0.0_r3/client/commandline.cpp#106
[adb-shell]: https://android.googlesource.com/platform/packages/modules/adb/+/refs/tags/android-13.0.0_r3/client/commandline.cpp#160
[adb-push]: https://android.googlesource.com/platform/packages/modules/adb/+/refs/tags/android-13.0.0_r3/client/commandline.cpp#142
[adb-pull]: https://android.googlesource.com/platform/packages/modules/adb/+/refs/tags/android-13.0.0_r3/client/commandline.cpp#148
[adb-wait-for]: https://android.googlesource.com/platform/packages/modules/adb/+/refs/tags/android-13.0.0_r3/client/commandline.cpp#215
[adb-get-state]: https://android.googlesource.com/platform/packages/modules/adb/+/refs/tags/android-13.0.0_r3/client/commandline.cpp#219
[adb-get-serialno]: https://android.googlesource.com/platform/packages/modules/adb/+/refs/tags/android-13.0.0_r3/client/commandline.cpp#220
[adb-get-devpath]: https://android.googlesource.com/platform/packages/modules/adb/+/refs/tags/android-13.0.0_r3/client/commandline.cpp#221
[adb-start-server]: https://android.googlesource.com/platform/packages/modules/adb/+/refs/tags/android-13.0.0_r3/client/commandline.cpp#237
[adb-kill-server]: https://android.googlesource.com/platform/packages/modules/adb/+/refs/tags/android-13.0.0_r3/client/commandline.cpp#238
[adb-reconnect]: https://android.googlesource.com/platform/packages/modules/adb/+/refs/tags/android-13.0.0_r3/client/commandline.cpp#239
[adb-reconnect-device]: https://android.googlesource.com/platform/packages/modules/adb/+/refs/tags/android-13.0.0_r3/client/commandline.cpp#240
[adb-reconnect-offline]: https://android.googlesource.com/platform/packages/modules/adb/+/refs/tags/android-13.0.0_r3/client/commandline.cpp#241
