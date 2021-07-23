# Build Workstation

Workstation (`workstation`) is an open source reference design for Fuchsia.
Workstation is not a consumer-oriented product. Workstation is a tool for
developers and enthusiasts to explore Fuchsia and experiment with evolving
concepts and features.

Workstation does not come with strong security, privacy, or robustness
guarantees. Bugs and rapid changes are expected – to help improve Fuchsia,
please [file bugs and send feedback][report-issue]. If interested, you can
configure your Workstation to receive
[automatic updates](#configure-automatic-updates).

## Get started with Workstation {#get-started-with-workstation}

To get started with Workstation, you need to be familiar with how to get the
Fuchsia source code, build Fuchsia images, and run Fuchsia on a device or
emulator – the instructions in this section are based on the
[Get started with Fuchsia][get-started-with-fuchsia] flow.

Workstation is designed to be used with an Intel NUC or the Fuchsia emulator
(FEMU).

*   {Intel NUC}

    To install Workstation on an Intel NUC, do the following:

    1.  Complete the [Download the Fuchsia source code][get-fuchsia-source]
        guide.
    2.  As part of [Configure and Build Fuchsia][build-fuchsia], set your build
        configuration to use the following Workstation product:

        ```posix-terminal
        fx set workstation.x64 --release
        ```

    3.  Complete the [Install Fuchsia on a NUC][intel-nuc] guide.

*   {FEMU}

    To try Workstation on the Fuchsia emulator, do the following:

    1.  Complete the [Download the Fuchsia source code][get-fuchsia-source]
        guide.
    2.  As part of [Configure and Build Fuchsia][build-fuchsia], set your build
        configuration to use the following Workstation product:

        ```posix-terminal
        fx set workstation.qemu-x64 --release
        ```

    3.  Complete the [Start the Fuchsia emulator][start-femu] guide.

## Configure automatic updates {#configure-automatic-updates}

Important: The instructions in this section configure your Fuchsia device to be
automatically updated with experimental releases. Use of the update servers is
governed by [Google APIs Terms of Service][google-apis-tos]{:.external} and data
collected by Google is handled in accordance with the
[Google Privacy Policy][google-privacy-policy]{:.external}.

Once you have Workstation running on a hardware device (for instance, Intel
NUC), you can choose to enroll the device for OTA (Over The Air) updates. These
updates ensure that your Fuchsia device automatically receives the latest
versions of the operating system and packages associated with Workstation.

To bootstrap your Fuchsia device into receiving updates, you need to include an
OTA configuration package into your build, then pave it to your device.

To set your Workstation build configuration with automatic update, run the
following command:

```posix-terminal
fx set workstation.x64 --release --with-base //src/workstation:{{ '<var>' }}BUILD_TARGET{{ '</var>' }}
```

Replace the following:

*   `BUILD_TARGET`: Determines which release channel to be used by your device.
    For more information, see
    [Configure release channels](#configure-release-channels).

For example,

```posix-terminal
fx set workstation.x64 --release --with-base //src/workstation:stable
```

Once your new build configuration is set, [build Fuchsia][build-fuchsia] and
[pave your device][paving] to begin receiving updates. Your device now
automatically polls for updates every 60 minutes. The device runs the version of
Workstation that you built, as opposed to a release build, until the device
receives its first update.

### Check for updates manually {#check-for-updates-manually}

To manually check for a new release, run the following command on the device's
shell:

```posix-terminal
update check-now --monitor
```

If there is a new update, this command reboots the device into the new release
after the download completes.

### Configure release channels {#configure-release-channels}

When you set the build configuration to receive updates, you can choose which
release channel your device receives updates from.

The available channels are:

*   **Stable** – A branch promoted on a regular basis from Beta.

    *   Build target: `//src/workstation:stable`

*   **Beta**– A branch promoted on a weekly basis as assessed by Fuchsia’s
    testing process. This branch may have more known issues or bugs than the
    stable channel.

    *   Build target: `//src/workstation:beta`

To configure the release channel on your device, follow the instructions in
[Configure automatic updates](#configure-automatic-updates) with the build
target of your choice.

<!-- Reference links -->

[report-issue]: /docs/contribute/report-issue.md
[get-started-with-fuchsia]: /docs/get-started
[get-fuchsia-source]: /docs/get-started/get_fuchsia_source.md
[build-fuchsia]: /docs/get-started/build_fuchsia.md
[intel-nuc]: /docs/development/hardware/intel_nuc.md
[start-femu]: /docs/get-started/set_up_femu.md
[paving]: /docs/development/hardware/paving.md
[google-apis-tos]: https://developers.google.com/terms
[google-privacy-policy]: https://policies.google.com/privacy
