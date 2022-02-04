# Configuration files for the text input tests

- `ignore_real_devices`: instructs the input pipeline to ignore a missing /dev/class/input
  directory. Useful in tests that don't have one.

- `scenic_config`: a scenic configuration that forces the use of flatland.  Due to
  http://fxbug.dev/92839, this is not yet used.
