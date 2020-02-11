# Static data for tests

## Contents of this package

* `empty.tar.gz`: A gzipped tarball containing no files. Checking in this file
  is preferable to creating it at test time for the following reasons:
  * Creating an empty tarball in a cross-platform way is not straightforward.
  * Controlling the empty tarball ensures that this file always hashes to the
    same value.

* `meta/manifest.json`: A simplified core SDK manifest.
