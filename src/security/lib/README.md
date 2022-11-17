# Fuchsia Security: Libraries
## Overview
This directory contains all libraries owned by the Fuchsia security team. Not
all of these libraries are intended for general consumption please consult the
security team before including them in a new project.

- Testing libraries should be placed in [//src/security/testing](//src/security/testing)

## Library Descriptions
* [fcrypto](//src/security/lib/fcrypto): Harder-to-misuse C++ library providing
  cryptographic primitives wrapping boringssl routines intended to support
  zxcrypt.
* [fuchsia-tcti](//src/security/lib/fuchsia-tcti): The Fuchsia implementation of
  the TPM Command Transmission interface. This is consumed by our port of
  `//third_party/tpm2-tss` to enable the TCG TPM2 Software Stack (TSS2) to work
  on Fuchsia.
* [fuchsia-tpm-protocol](//src/security/lib/fuchsia-tpm-protocol): The
  implementation of the `fuchsia.tpm` FIDL interfaces. This allows the
  implementation to be shared by the `cr50_agent` and the `tpm_agent`. This
  protocol allows for provisioning and deprovisioning of the TPM.
* [keysafe](//src/security/lib/keysafe): Keysafe trusted application interface,
  which defines the list of supported commands and their parameters.
* [kms-stateless](//src/security/lib/kms-stateless): A stateless (does not
  persist anything by itself) key management service built on top of the
  KeySafe TA. Currently supports hardware protected key derivation and rotations.
* [scrutiny](//src/security/lib/scrutiny): Scrutiny is a static analysis
  library for Fuchsia . It is a powerful framework that aims to allow you to
  inspect all aspects of a built product and build verifiers on top of it which
  can guarantee properties are maintained at build time. For instance we use
  this library today to validate that all protocol routes in the system are
  valid or to prevent unwanted files sneaking into bootfs. See `ffx scrutiny`
  for all the verifiers implemented on top of this library.
* [tee](//src/security/lib/tee):  Client API that allows Fuchsia to invoke an
  security service provided by a TA in TEE. The client API confirms to
  GlobalPlatform standard and currently handles TA session establishment,
  invoking a command, shared memory management etc.
* [zxcrypt](//src/security/lib/zxcrypt): An encrypted (but not authenticated)
  filter block device core and supporting client libraries. minfs, where used,
  is generally configured to sit atop a zxcrypt-encrypted block device to
  protect mutable data.

