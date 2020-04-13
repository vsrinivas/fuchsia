#!/bin/bash

# Tell dump_syms to (1) use "<_>" as module name for modules other than shared
# libraries (that don't have a name at runtime on Fuchsia) and (2) to set the
# OS to "Fuchsia" in the output Breakpad file.
"$1" -n "<_>" -o "Fuchsia" "$2" > "$3"
