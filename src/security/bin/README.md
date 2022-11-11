# Fuchsia Security: Binaries
## Overview
This directory contains all source code that results in a component, package
or other binary that is intended to be included in some assembled version
of Fuchsia.

- Host only tools should instead be placed in [//src/security/tools](//src/security/tools)
- Integration tests should be placed in [//src/security/tests](//src/security/tests)

## Project Descriptions
* [credential\_manager](//src/security/bin/credential_manager): Launched at
  `/core/account/credential_manager` and serves the `fuchsia.identity.credential` FIDL
  interfaces. The services provided by this component are consumed by the
  `password_authenticator` for storing and retrieving user credentials.
* [cr50\_agent](//src/security/bin/cr50_agent): Launched at `/bootstrap/cr50_agent` and
  serves the `fuchsia.tpm.cr50` and `fuchsia.tpm` FIDL interfaces. This agent acts as
  a resource manager between the underlying driver implementation and the rest of the
  system.
* [root\_ssl\_certificates](//src/security/bin/root_ssl_certificates): Fuchsia's
  TLS root CA certificates (a.k.a. truststore). It serves as a resource package
  for components that use TLS.
* [tpm\_agent](//src/security/bin/tpm_agent): (WIP) Will be launched at `/bootstrap/tpm_agent`
  on devices that have a TPM2.0 but not a CR50. It will serve the `fuchsia.tpm` FIDL interface.
  This agent acts as a resource manager between the underlying driver implementation and
  the rest of the system.
* [tee\_manager](//src/security/bin/tee_manager): Fuchsia - TEE communication
  stack. Marshals trusted application invocations; handles secure storage RPCs.
* [syscall\_checker](//src/security/bin/syscall_checker): Prints whether certain
  security sensitive system calls are enabled or disabled. Used in manual testing.
