## Test Driver

This project aims to be a 1-click solution for executing CTS tests outside of
the Fuchsia tree.

### Running the CTS

1) Add `//sdk/cts` to your `fx args`:

       fx set terminal.qemu-x64 --with //sdk/cts
       fx build

2) Create a workspace for downloading artifacts:

       mkdir -p $CTS_WORKSPACE

  The following commands will assume your workspace is located at $CTS_WORKSPACE

3) Choose an SDK to target:

  Currently, SDK version strings look like the following:
  `X.YYYYMMDD.X.X`

  For example: This SDK version `3.20210308.3.1` matches this CIPD entry:
  https://chrome-infra-packages.appspot.com/p/fuchsia/sdk/gn/linux-amd64/+/GMIy21KF7lUXfC-C75Lwt0TG7fx2tVjk_5PRx_KT0kkC

  You can view all SDK releases in CIPD:
  https://chrome-infra-packages.appspot.com/p/fuchsia/sdk/gn/linux-amd64/+/

  The CTS is most beneficial when running against SDK versions that are _newer_
  than the test code, but the tool doesn't prevent you from choosing any
  SDK version.

4) Run the tool using `fx`:

       fx cts --sdk_version $SDK_VERSION --workspace $CTS_WORKSPACE --manifest $MANIFEST_PATH

  e.g. `fx cts --sdk_version 3.20210308.2.1 --workspace ~/cts --manifest cts_manifest.json`

  Currently it just downloads the specified SDK and exits. Soon, this will start
  up an emulator, download all necessary artifacts to your workspace, and
  execute the CTS tests.
