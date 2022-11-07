# verify_release.sh

## Overview

A CTS release may fail for several reasons. A few possible situations are
below:

When **generating the CTS archive**:
  *  `//out/default/ctf_artifacts.json` may have been populated incorrectly.
Fuchsia Infra uses this list to collect the CTS archive contents, and if a file
in that list is missing, it may cause the process to halt.

When **rolling a new CTS release into GI**:
  *  A test dependency may need to have the CTS version string appended, so it
doesn't conflict with the same tests running at TOT.

Verifying the CTS release is an important step that should happen before
submitting any significant changes to the Fuchsia CTS. The script in this
directory should make it easy for you to verify the release locally.

## Running the script

### 1) Start an emulator

Launch a new terminal window or tab, and run the following command:

```
fx vdl start --headless
```

Leave this terminal window running in the background.

### 2) Start a fuchsia package server.

Launch a new terminal window or tab, and run the following command:

```
fx serve
```

Leave this terminal window running in the background.

### 3) Run the verify_release script

Launch a new terminal window or tab, and run the following command:

```
./$FUCHSIA_HOME/sdk/ctf/build/scripts/verify_release/verify_release.py
```

After a brief pause, the CTS test results should print to the terminal window.
If everything worked, it will say "X passed, 0 failed" near the end of the
test output.

## How this script works

The verify_release.py script does the following:

```
# Build the CTS.
fx set core.x64 --with //sdk/ctf --args cts_version=\"test\"
fx build

# Copy the CTS archive to your prebuilt directory
sudo rm -rf prebuilt/cts/test
mkdir prebuilt/cts/test
cp -r out/default/cts prebuilt/cts/test/cts

# Copy a build file over to correctly link into the prebuilt test artifacts
cp sdk/ctf/build/scripts/verify_release/_BUILD.gn prebuilt/cts/test/BUILD.gn

# Modify that build file, replacing all instances of `{cts_version}` with `test`

# Run the tests.
fx set core.x64 --with //prebuilt/cts/test:tests
fx test
```
