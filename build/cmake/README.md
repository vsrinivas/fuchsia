This directory contains files that will be useful when building
third-party projects that use CMake as their build configuration tool.

  - HostLinuxToolchain.cmake:
      A CMake toolchain file used to build host Linux binaries with our
      prebuilt toolchain and sysroot. These are guaranteed to work on Debian
      wheezy (roughly equivalent to Ubuntu 14.04) and above, as required by
      our CI bots.

  - ToolchainCommon.cmake:
      Provides common functions for toolchain related scripts.
      Included by HostLinuxToolchain.cmake.
