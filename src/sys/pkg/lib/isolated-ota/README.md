isolated-ota
============

The `isolated-ota` library provides a simple interface that allows a Fuchsia
system to be installed over the air to a given blobfs and paver from a provided
TUF repository and channel.

To use it, you need to make sure your image includes the package
`//src/sys/pkg/lib/isolated-swd:isolated-swd-components`.

It does this by setting up the software delivery stack:
1. `pkgfs` is launched against the provided blobfs.
2. `pkg-cache` is launched, using the `pkgfs` from step 1.
3. `pkg-resolver` is launched, using the provided repository configuration and
   channel, along with `pkg-cache` from step 2.
4. If Omaha configuration is provided (an Omaha app id, and a URL to use for the
   Omaha server), the `omaha-client` state machine is launched. It performs an
   update check once, and the Omaha state machine calls the system-updater with
   the update package URI returned by Omaha.
5. If no Omaha configuration is provided, `isolated-ota` launches the system
   updater directly, using the default update URL.
6. The system updater runs an OTA, resolving all of the packages in the new
   system using the `pkg-resolver` from step 3, and paving the images in the
   update package using the provided paver.
