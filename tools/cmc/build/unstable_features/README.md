# unstable_features

This directory contains a BUILD file with GN groups that act as allowlists for unstable
CFv2 CML features.

The GN component build rules will add a dependency on the group associated with an unstable
feature. If the component using this feature is not in the allowlist, the build will fail.

This is meant to limit the use of features that are unstable and likely to change in the
future.
