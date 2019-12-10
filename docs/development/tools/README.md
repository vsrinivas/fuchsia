There are various Fuchsia tools that can help you while you develop for Fuchsia:

*   [Banjo](/docs/development/tools/banjo-tutorial.md)

    This tool is a transpiler (like FIDL's `fidlc`) that converts an interface
    definition language (IDL) into target language specific files.

*   [Debugger](/docs/development/debugger/README.md)

    This tool is a debugger for C, C++, and Rust code that runs on
    Fuchsia for either 64-bit ARM or 64-bit x86 architectures.

*   [`fidlcat`](/docs/development/tools/fidl_inspecting/README.md)

    This tool is a FIDL connection monitor. `fidlcat` can attach to
    or launch a process on a Fuchsia device, and reports the FIDL traffic
    of that process.

*   [`iquery`](/docs/development/inspect/iquery.md)

    This tool is a utility program that inspects component nodes exposed over
    the [inspect API](/docs/development/inspect/gsw-inspect.md). `iquery` accepts
    a list of paths to process, and how they are processed depends on the
    `MODE` setting and options.

*  [QEMU](/docs/development/emulator/qemu.md)

   This emulator lets you run Zircon. You can install QEMU with prebuilt libraries
   or build it locally.

*  [System monitor](/docs/development/tools/system_monitor/README.md)

   This tool displays the vital signs of a Fuchsia device and its processes.
   The system monitor collects samples of device-wide and per-process usage of
   CPU, memory usage, and processes. You can then view these samples from the
   host machine.
