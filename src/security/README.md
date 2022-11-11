# Fuchsia Security

The Fuchsia Security team owns components, tools, tests and policy to maintain
and improve the security of the Fuchsia operating system. This directory
follows a particular structure to keep things organized.

When adding code to this directory follow the following conventions:

* [//src/security/bin](//src/security/bin): Components or binaries that are
  bundled into any product definition.
* [//src/security/lib](//src/security/lib): Libraries that the security team
  owns. Be sure to set an appropriate visibility list. This folder should not
  be used for test-only libraries instead see the testing folder.
* [//src/security/policy](//src/security/policy): For any runtime or
  compile-time configuration for base policies.
* [//src/security/tests](//src/security/tests): All integration tests and
  other testing binaries.
* [//src/security/testing](//src/security/testing): All generic testing
  libraries and mocks.
* [//src/security/tools](//src/security/tools): For any tools or scripts that
  are not included as part of any build.
