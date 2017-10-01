# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Initialize gdb for debugging Fuchsia code."""

import gdb
import glob
import os


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

# The top level zircon build directory
_TOP_ZIRCON_BUILD_DIR = "out/build-zircon"

# Prefix of zircon build directories within _TOP_ZIRCON_BUILD_DIR.
_ZIRCON_BUILD_SUBDIR_PREFIX = "build-zircon-"

# True if fuchsia support has been initialized.
_INITIALIZED_FUCHSIA_SUPPORT = False

# The prefix for fuchsia commands.
_FUCHSIA_COMMAND_PREFIX = "fuchsia"


class _FuchsiaPrefix(gdb.Command):
    """Prefix command for Fuchsia-specific commands."""

    def __init__(self):
        super(_FuchsiaPrefix, self).__init__(
            _FUCHSIA_COMMAND_PREFIX, gdb.COMMAND_USER, prefix=True)


class _FuchsiaSetPrefix(gdb.Command):
    """Prefix "set" command for Fuchsia parameters."""

    def __init__(self):
        super(_FuchsiaSetPrefix, self).__init__(
            "set %s" % (_FUCHSIA_COMMAND_PREFIX), gdb.COMMAND_USER,
            prefix=True)


class _FuchsiaShowPrefix(gdb.Command):
    """Prefix "show" command for Fuchsia parameters."""

    def __init__(self):
        super(_FuchsiaShowPrefix, self).__init__(
            "show %s" % (_FUCHSIA_COMMAND_PREFIX), gdb.COMMAND_USER,
            prefix=True)

    def invoke(self, from_tty):
        # TODO(dje): Show all the parameters, a la cmd_show_list.
        pass


class _FuchsiaVerbosity(gdb.Parameter):
    """Verbosity for Fuchsia gdb support.

    There are four levels of verbosity:
    0 = off
    1 = minimal
    2 = what a typical user might want to see
    3 = everything, intended for maintainers only
    """
    # Note: While not every verbosity level is exercised today, these levels
    # are convention in Google's internal gdb.

    set_doc = "Set level of Fuchsia verbosity."
    show_doc = "Show level of Fuchsia verbosity."

    def __init__(self):
        super(_FuchsiaVerbosity, self).__init__(
            "%s verbosity" % (_FUCHSIA_COMMAND_PREFIX),
            gdb.COMMAND_FILES, gdb.PARAM_ZINTEGER)
        # Default to basic informational messages to help users know
        # what's going on.
        self.value = 1

    def get_show_string(self, pvalue):
        return "Fuchsia verbosity is " + pvalue + "."

    def get_set_string(self):
        # Ugh.  There doesn't seem to be a way to implement a gdb parameter in
        # Python that will be silent when the user changes the value.
        return "Fuchsia verbosity been set to %d." % (self.value)


def _IsFuchsiaFile(objfile):
    """Return True if objfile is a Fuchsia file."""
    # TODO(dje): Not sure how to effectively achieve this.
    # Assume we're always debugging a Fuchsia program for now.
    # If the user wants to debug native programs, s/he can use native gdb.
    return True


def _ClearObjfilesHandler(event):
    """Reset debug information tracking when all objfiles are unloaded."""
    event.progspace.seen_exec = False


def _FindSysroot(arch):
    """Return the path to the sysroot for arch."""
    if arch == "x86-64":
        suffix = "-x86-64"
    elif arch == "arm64":
        suffix = "-arm64"
    else:
        assert(False)
    print ("TRYING: %s/%s*%s" % (
        _TOP_ZIRCON_BUILD_DIR, _ZIRCON_BUILD_SUBDIR_PREFIX, suffix))
    for filename in glob.iglob("%s/%s*%s" % (
            _TOP_ZIRCON_BUILD_DIR, _ZIRCON_BUILD_SUBDIR_PREFIX, suffix)):
        return "%s/sysroot" % (filename)
    return None


def _NewObjfileHandler(event):
    """Handle new objfiles being loaded."""
    # TODO(dje): Use this hook to automagically fetch debug info.

    verbosity = gdb.parameter(
        "%s verbosity" % (_FUCHSIA_COMMAND_PREFIX))
    if verbosity >= 3:
        print "Hi, I'm the new_objfile event handler."

    objfile = event.new_objfile
    progspace = objfile.progspace
    # Assume the first objfile we see is the main executable.
    # There's nothing else we can do at this point.
    seen_exec = hasattr(progspace, "seen_exec") and progspace.seen_exec

    # Early exit if nothing to do.
    # We don't handle multiple arches so we KISS.
    if seen_exec:
        if verbosity >= 3:
            print "Already seen exec, ignoring: %s" % (basename)
        return
    progspace.seen_exec = True

    filename = objfile.username
    basename = os.path.basename(filename)
    if objfile.owner is not None:
        if verbosity >= 3:
            print "Separate debug file, ignoring: %s" % (basename)
        return

    # If we're debugging a native executable, unset the solib search path.
    if not _IsFuchsiaFile(objfile):
        if verbosity >= 3:
            print "Debugging non-Fuchsia file: %s" % (basename)
        print "Note: Unsetting solib-search-path."
        gdb.execute("set solib-search-path")
        return

    # The sysroot to use is dependent on the architecture of the program.
    # This is needed to find ld.so debug info.
    # TODO(dje): IWBN to not need ld.so debug info.
    # TODO(dje): IWBN if objfiles exposed their arch field.
    arch_string = gdb.execute("show arch", to_string=True)
    if arch_string.find("aarch64") >= 0:
        # Alas there are different directories for different arm64 builds
        # (qemu, rpi3, etc.). Pick something, hopefully this can go away soon.
        sysroot_dir = _FindSysroot("arm64")
    elif arch_string.find("x86-64") >= 0:
        sysroot_dir = _FindSysroot("x86-64")
    else:
        print "WARNING: unsupported architecture\n%s" % (arch_string)
        return

    # TODO(dje): We can't use sysroot to find ld.so.1 because it doesn't
    # have a path on Fuchsia. Plus files in Fuchsia are intended to be
    # "ephemeral" by nature. So we punt on setting sysroot for now, even
    # though IWBN if we could use it.
    if sysroot_dir:
        solib_search_path = "%s/debug-info" % (sysroot_dir)
        print "Note: Setting solib-search-path to %s" % (solib_search_path)
        gdb.execute("set solib-search-path %s" % (solib_search_path))
    else:
        print "WARNING: could not find sysroot directory"


def _InitializeFuchsiaObjfileTracking():
    # We *need* solib-search-path set so that we can find debug info for
    # ld.so.1. Otherwise it's game over for a usable debug session:
    # We won't be able to set a breakpoint at the dynamic linker breakpoint
    # and we won't be able to relocate the program (all Fuchsia executables
    # are PIE). However, we don't necessarily know which architecture we're
    # debugging yet so we don't know which directory to set the search path
    # to. To solve this we hook into the "new objfile" event.
    # This event can also let us automagically fetch debug info for files
    # as they're loaded (TODO(dje)).
    gdb.events.clear_objfiles.connect(_ClearObjfilesHandler)
    gdb.events.new_objfile.connect(_NewObjfileHandler)


class _SetFuchsiaDefaults(gdb.Command):
    """Set GDB parameters to values useful for Fuchsia code.

    Usage: set-fuchsia-defaults

    These changes are made:
      set non-stop on
      set target-async on
      set remotetimeout 10
      set sysroot # (set to empty path)

    Fuchsia gdbserver currently supports non-stop only (and even that support
    is preliminary so heads up).
    """

    def __init__(self):
        super(_SetFuchsiaDefaults, self).__init__(
            "%s set-defaults" % (_FUCHSIA_COMMAND_PREFIX),
            gdb.COMMAND_DATA)

    # The name and parameters of this function are defined by GDB.
    # pylint: disable=invalid-name
    # pylint: disable=unused-argument
    def invoke(self, arg, from_tty):
        """GDB calls this to perform the command."""
        # We don't need to tell the user about everything we do.
        # But it's helpful to give a heads up for things s/he may trip over.
        print "Note: Enabling non-stop, target-async."
        gdb.execute("set non-stop on")
        gdb.execute("set target-async on")
        gdb.execute("set remotetimeout 10")

        # The default is "target:" which will cause gdb to fetch every dso,
        # which is ok sometimes, but for right now it's a nuisance.
        print "Note: Unsetting sysroot."
        gdb.execute("set sysroot")


def _InstallFuchsiaCommands():
    # We don't do anything with the result, we just need to call
    # the constructor.
    _FuchsiaPrefix()
    _FuchsiaSetPrefix()
    _FuchsiaShowPrefix()
    _FuchsiaVerbosity()
    _SetFuchsiaDefaults()


def initialize():
    """Set up GDB for debugging Fuchsia code.

    This function is invoked via gdb's "system.gdbinit"
    when it detects it is being started in a fuchsia tree.

    It is ok to call this function multiple times, but only the first
    is effective.

    Returns:
        Nothing.
    """

    global _INITIALIZED_FUCHSIA_SUPPORT
    if _INITIALIZED_FUCHSIA_SUPPORT:
        print "Fuchsia support already loaded."
        return
    _INITIALIZED_FUCHSIA_SUPPORT = True

    _InstallFuchsiaCommands()
    _InitializeFuchsiaObjfileTracking()
    print "Setting fuchsia defaults. 'help fuchsia set-defaults' for details."
    gdb.execute("fuchsia set-defaults")
