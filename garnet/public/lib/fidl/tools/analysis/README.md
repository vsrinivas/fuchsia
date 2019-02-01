# Scripts

These are scripts that are useful for analyzing the corpus of FIDL libraries in
the Fuchsia tree. They operate on the `.fidl.json` IR in the out directory. They
should be run with `fx exec SCRIPTNAME` so that they can find the out directory.

The library `ir.py` finds and parses the IR files and makes them available in a
somewhat pythonic interface.