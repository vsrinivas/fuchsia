# Magenta Makefile Options

The following options can be passed to **make** when building Magenta:

* **BOOTFS_DEBUG_MODULES**: See [debugging tips](debugging/tips.md).

* **DEBUG**: This specifies the debug level.  The default is 2.  Setting
**DEBUG=1** will disable some debugging code (such as **DEBUG_ASSERT()**),
while setting **DEBUG=0** will disable more debugging code.

* **ENABLE_ACPI_DEBUG**: See [ACPI debugging](debugging/acpi.md).

* **GLOBAL_DEBUGFLAGS**: See [debugging tips](debugging/tips.md).

* **GOMACC**: Path to the Goma compiler wrapper, **gomacc**, for use within
Google for distributed builds.  The default is not to use Goma.

* **USE_ASAN**: Set **USE_ASAN=1** to enable using ASan (the address
sanitizer).

* **USE_CLANG**: Set **USE_CLANG=1** to enable building with Clang.
Otherwise, the default is to use GCC as the compiler.

* **V**: Set **V=1** to tell the build system to print each command that
**make** executes.  Otherwise, the build system only prints a short summary
of each build step.
