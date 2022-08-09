# process-resolver

This component implements the `fuchsia.process.Resolver` protocol. This protocol
is used by the `#!resolve` trampoline implemented in `fdio` that can launch a binary
from any fuchsia package.

process-resolver works in one of two modes:
* If the `auto_update_packages` feature is enabled at build time, process-resolver will use
  `fuchsia.pkg.PackageResolver` to resolve packages from the universe set.
* Otherwise, process-resolver will use `/pkgfs/packages` to resolve packages from the base
  set only.

The `auto_update_packages` build flag is controlled by the Security team and is located at
`//build/security.gni`.
