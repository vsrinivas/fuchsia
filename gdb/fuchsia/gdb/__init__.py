# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Initialize gdb for debugging Fuchsia code."""

import gdb


# Extract the GDB version number so scripts can easily examine it.
# We export four variables:
# GDB_MAJOR_VERSION, GDB_MINOR_VERSION, GDB_PATCH_VERSION, GDB_GOOGLE_VERSION.
# The first three are standard, e.g. 7.10.1.
# GDB_GOOGLE_VERSION is the N in "-gfN" Fuchsia releases.
# A value of zero means this isn't a Google Fuchsia gdb.
_GDB_DASH_VERSION = gdb.VERSION.split("-")
_GDB_DOT_VERSION = _GDB_DASH_VERSION[0].split(".")
GDB_MAJOR_VERSION = int(_GDB_DOT_VERSION[0])
GDB_MINOR_VERSION = int(_GDB_DOT_VERSION[1])
if len(_GDB_DOT_VERSION) >= 3:
    GDB_PATCH_VERSION = int(_GDB_DOT_VERSION[2])
else:
    GDB_PATCH_VERSION = 0
GDB_GOOGLE_VERSION = 0
if len(_GDB_DASH_VERSION) >= 2:
    if _GDB_DASH_VERSION[1].startswith("gf"):
        try:
            GDB_GOOGLE_VERSION = int(_GDB_DASH_VERSION[1][2:])
        except ValueError:
            pass


class SetFuchsiaDefaults(gdb.Command):
    """Set GDB parameters to values useful for Fuchsia code.

    Usage: set-fuchsia-defaults

    These changes are made:
      set non-stop on
      set target-async on
      set remotetimeout 10

    Fuchsia gdbserver currently supports non-stop only (and even that support
    is preliminary so heads up).
    """

    def __init__(self):
        super(SetFuchsiaDefaults, self).__init__("set-fuchsia-defaults",
                                                 gdb.COMMAND_DATA)

    # The name and parameters of this function are defined by GDB.
    # pylint: disable=invalid-name
    # pylint: disable=unused-argument
    def invoke(self, arg, from_tty):
        """GDB calls this to perform the command."""
        gdb.execute("set non-stop on")
        gdb.execute("set target-async on")
        gdb.execute("set remotetimeout 10")

        sysroot_dir = "out/sysroot/x86_64-fuchsia"
        # TODO(dje): We can't use sysroot to find ld.so.1 because it doesn't
        # have a path on Fuchsia. Plus files in Fuchsia are intended to be
        # "ephemeral" by nature. So we punt on setting sysroot for now, even
        # though IWBN if we could use it.
        gdb.execute("set solib-search-path %s/debug-info" % sysroot_dir)
        # The default is "target:" which will cause gdb to fetch every dso,
        # which is ok sometimes, but for right now it's a nuisance.
        gdb.execute("set sysroot")


def initialize():
    SetFuchsiaDefaults()
    print "Setting fuchsia defaults. 'help set-fuchsia-defaults' for details."
    gdb.execute("set-fuchsia-defaults")
