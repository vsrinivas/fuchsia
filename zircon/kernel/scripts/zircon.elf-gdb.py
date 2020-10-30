# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# GDB support for zircon kernel.

# TODO(dje): gdb should let us use a better command class than COMMAND_DATA.
# TODO(dje): Add arm64 support.

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import sys
import gdb
import gdb.printing
import re
from gdb.unwinder import Unwinder

if sys.version_info > (3,):
    long = int

# The command prefix, passed in from gdbinit.py.
_ZIRCON_COMMAND_PREFIX = "zircon"
_KERNEL_EXCEPTION_UNWINDER_PARAMETER = "kernel-exception-unwinder"

_THREAD_MAGIC = 0x74687264
_BOOT_MAGIC = 0x544f4f42
_KERNEL_BASE_ADDRESS = "KERNEL_BASE_ADDRESS"

print("Loading zircon.elf-gdb.py ...")


def _get_architecture():
    # TODO(dje): gdb doesn't provide us with a simple way to do this.
    return gdb.execute("show architecture", to_string=True)


def _is_x86_64():
    """Return True if we're on an x86-64 platform."""
    return re.search(r"x86-64", _get_architecture())


def _is_arm64():
    """Return True if we're on an aarch64 platform."""
    return re.search(r"aarch64", _get_architecture())


# The default is 2 seconds which is too low.
# But don't override something the user set.
# [If the user set it to the default, too bad. :-)]
# TODO(dje): Alas this trips over upstream PR 20084.
#_DEFAULT_GDB_REMOTETIMEOUT = 2
#if int(gdb.parameter("remotetimeout")) == _DEFAULT_GDB_REMOTETIMEOUT:
#  gdb.execute("set remotetimeout 10")


class _ZirconPrefix(gdb.Command):
    """zircon command prefix"""

    def __init__(self):
        super(_ZirconPrefix, self).__init__(
            "%s" % (_ZIRCON_COMMAND_PREFIX), gdb.COMMAND_DATA, prefix=True)


class _InfoZircon(gdb.Command):
    """info zircon command prefix"""

    def __init__(self):
        super(_InfoZircon, self).__init__(
            "info %s" % (_ZIRCON_COMMAND_PREFIX), gdb.COMMAND_DATA, prefix=True)


class _SetZircon(gdb.Command):
    """set zircon command prefix"""

    def __init__(self):
        super(_SetZircon, self).__init__(
            "set %s" % (_ZIRCON_COMMAND_PREFIX), gdb.COMMAND_DATA, prefix=True)


class _ShowZircon(gdb.Command):
    """show zircon command prefix"""

    def __init__(self):
        super(_ShowZircon, self).__init__(
            "show %s" % (_ZIRCON_COMMAND_PREFIX), gdb.COMMAND_DATA, prefix=True)


class _ZirconMaxInfoThreads(gdb.Parameter):
    """Parameter to limit output of "info zircon threads" command.

  This parameter is an escape hatch to catch corrupted lists.
  We don't want "info zircon threads" to loop forever.

  The value is the maximum number of threads that will be printed.
  """

    set_doc = "Set the maximum number of zircon threads to print."
    show_doc = "Show the maximum number of zircon threads to print."

    def __init__(self):
        super(_ZirconMaxInfoThreads, self).__init__(
            "%s max-info-threads" % (_ZIRCON_COMMAND_PREFIX), gdb.COMMAND_DATA,
            gdb.PARAM_UINTEGER)
        self.value = 1000

    def get_show_string(self, pvalue):
        return "Maximum number of threads to print is " + pvalue + "."

    def get_set_string(self):
        # Ugh.  There doesn't seem to be a way to implement a gdb parameter in
        # Python that will be silent when the user changes the value.
        if self.value is None:
            value = "unlimited"
        else:
            value = self.value
        return "Maximum number of threads to print been set to %s." % (value)


class _ZirconMaxInfoProcesses(gdb.Parameter):
    """Parameter to limit output of "info zircon processes" command.

  This parameter is an escape hatch to catch corrupted lists.
  We don't want "info zircon processes" to loop forever.

  The value is the maximum number of processes that will be printed.
  """

    set_doc = "Set the maximum number of zircon processes to print."
    show_doc = "Show the maximum number of zircon processes to print."

    def __init__(self):
        super(_ZirconMaxInfoProcesses, self).__init__(
            "%s max-info-processes" % (_ZIRCON_COMMAND_PREFIX),
            gdb.COMMAND_DATA, gdb.PARAM_UINTEGER)
        self.value = 1000

    def get_show_string(self, pvalue):
        return "Maximum number of processes to print is " + pvalue + "."

    def get_set_string(self):
        # Ugh.  There doesn't seem to be a way to implement a gdb parameter in
        # Python that will be silent when the user changes the value.
        if self.value is None:
            value = "unlimited"
        else:
            value = self.value
        return "Maximum number of processes to print been set to %s." % (value)


class _ZirconMaxInfoHandles(gdb.Parameter):
    """Parameter to limit output of "info zircon handles" command.

  This parameter is an escape hatch to catch corrupted lists.
  We don't want "info zircon handles" to loop forever.

  The value is the maximum number of handles that will be printed.
  """

    set_doc = "Set the maximum number of zircon handles to print."
    show_doc = "Show the maximum number of zircon handles to print."

    def __init__(self):
        super(_ZirconMaxInfoHandles, self).__init__(
            "%s max-info-handles" % (_ZIRCON_COMMAND_PREFIX), gdb.COMMAND_DATA,
            gdb.PARAM_UINTEGER)
        self.value = 1000

    def get_show_string(self, pvalue):
        return "Maximum number of handles to print is " + pvalue + "."

    def get_set_string(self):
        # Ugh.  There doesn't seem to be a way to implement a gdb parameter in
        # Python that will be silent when the user changes the value.
        if self.value is None:
            value = "unlimited"
        else:
            value = self.value
        return "Maximum number of handles to print been set to %s." % (value)


def containerof(node_ptr, type_name, member_name):
    """Python version of zircon's containerof macro."""
    # TODO(dje): This could only be computed once.
    # For more popular types, compute all possible once.
    char_ptr = gdb.lookup_type("char").pointer()
    ptr = node_ptr.cast(char_ptr)
    type_object_ptr = gdb.lookup_type(type_name).pointer()
    offsetof = long(gdb.Value(0).cast(type_object_ptr)[member_name].address)
    return (ptr - offsetof).cast(type_object_ptr)


def _build_zircon_pretty_printers():
    pp = gdb.printing.RegexpCollectionPrettyPrinter("zircon")
    # Insert printer registration here.
    #pp.add_printer("foo", "^foo$", _ZirconFooPrinter)
    return pp


def register_zircon_pretty_printers(obj):
    if obj is None:
        obj = gdb
    gdb.printing.register_pretty_printer(
        obj, _build_zircon_pretty_printers(), replace=True)


def _get_thread_list():
    """ Return a list of all Thread threads.

  The result is constrained by "zircon max-info-threads".
  """
    threads = []
    head = gdb.parse_and_eval("&thread_list")
    t = head["next"]
    count = 0
    max_threads = gdb.parameter(
        "%s max-info-threads" % (_ZIRCON_COMMAND_PREFIX))
    int_type = gdb.lookup_type("int")
    ptr_size = int_type.pointer().sizeof
    # Note: A "corrupted" list can happen for a short time while an
    # element is being added/removed. And, in non-stop mode, the list can
    # change while we're at it. This isn't a problem in all-stop mode, but
    # non-stop mode is generally preferable. We'll see how this works in
    # practice.
    while t and t != head:
        if max_threads is not None and count >= max_threads:
            break
        # Catch misaligned pointers.
        # Casting to int shouldn't be necessary, but the python API doesn't
        # support creating an int from a pointer.
        # We assume the object is aligned at least as great as a pointer.
        if (t.cast(int_type) & (ptr_size - 1)) != 0:
            break
        # TODO(dje): Do a range check?
        thread_ptr = containerof(t, "thread", "thread_list_node")
        if thread_ptr["magic"] != _THREAD_MAGIC:
            break
        # TODO(dje): Move this to a routine, more list printers will want this.
        threads.append(thread_ptr)
        t = t["next"]
        count += 1
    return threads


def _get_process_list():
    """Return list of all processes.

  The result is constrained by "zircon max-info-processes".
  """
    processes = []
    head = gdb.parse_and_eval("&process_list")
    head = head["head_"]
    p = head
    count = 0
    max_processes = gdb.parameter(
        "%s max-info-processes" % (_ZIRCON_COMMAND_PREFIX))
    int_type = gdb.lookup_type("int")
    ptr_size = int_type.pointer().sizeof
    # Note: A "corrupted" list can happen for a short time while an
    # element is being added/removed. And, in non-stop mode, the list can
    # change while we're at it. This isn't a problem in all-stop mode, but
    # non-stop mode is generally preferable. We'll see how this works in
    # practice.
    while p:
        if max_processes is not None and count >= max_processes:
            break
        # Catch misaligned pointers.
        # Casting to int shouldn't be necessary, but the python API doesn't
        # support creating an int from a pointer.
        # We assume the object is aligned at least as great as a pointer.
        if (p.cast(int_type) & (ptr_size - 1)) != 0:
            break
        # TODO(dje): Do a range check?
        # TODO(dje): Move this to a routine, more list printers will want this.
        processes.append(p)
        p = p["next_"]
        count += 1
        if p == head:
            break
    return processes


def _get_handle_list(process):
    """Return list of all handles of process.

  The result is constrained by "zircon max-info-handles".
  """
    handles = []
    head = process["handles_"]
    head = head["head_"]
    h = head
    count = 0
    max_handles = gdb.parameter(
        "%s max-info-handles" % (_ZIRCON_COMMAND_PREFIX))
    int_type = gdb.lookup_type("int")
    ptr_size = int_type.pointer().sizeof
    # Note: A "corrupted" list can happen for a short time while an
    # element is being added/removed. And, in non-stop mode, the list can
    # change while we're at it. This isn't a problem in all-stop mode, but
    # non-stop mode is generally preferable. We'll see how this works in
    # practice.
    while h:
        if max_handles is not None and count >= max_handles:
            break
        # Catch misaligned pointers.
        # Casting to int shouldn't be necessary, but the python API doesn't
        # support creating an int from a pointer.
        # We assume the object is aligned at least as great as a pointer.
        if (h.cast(int_type) & (ptr_size - 1)) != 0:
            break
        # TODO(dje): Do a range check?
        # TODO(dje): Move this to a routine, more list printers will want this.
        handles.append(h)
        h = h["next_"]
        count += 1
        if h == head:
            break
    return handles


def _print_thread_summary(thread, number, tls_entry, user_thread_ptr_t):
    user_thread = thread["tls"][tls_entry]
    if user_thread:
        user_thread_ptr = user_thread.cast(user_thread_ptr_t)
        # TODO(dje): Why is str necessary here? Otherwise ->
        # "Cannot convert value to int."
        pid = str(user_thread_ptr["process_"]["id_"])
    else:
        pid = "kern"
    name = str(thread["name"].lazy_string().value()).strip('"')
    print(
        "%3d %5s %#16x %-32s %s" %
        (number, pid, thread.address, name, thread["state"]))


class _InfoZirconThreads(gdb.Command):
    """info zircon threads command

  This command prints a list of all zircon threads.
  TODO: Allow specifying which threads to print.
  """

    def __init__(self):
        super(_InfoZirconThreads, self).__init__(
            "info %s threads" % (_ZIRCON_COMMAND_PREFIX), gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        # Do this first to make sure the previous value gets cleared out.
        # There's no way to unset a convenience var, so KISS.
        gdb.execute("set $zx_threads = (Thread*[1]) { 0 }")
        tls_entry_lkuser = gdb.parse_and_eval("TLS_ENTRY_LKUSER")
        threads = _get_thread_list()
        num_threads = len(threads)
        # The array is origin-1-indexed. Have a null first entry to KISS.
        gdb.execute("set $zx_threads = (Thread*[%d]) { 0 }" % (num_threads + 1))

        # Populate the array first, before printing the summary, to make sure this
        # gets done even if there's an error during printing.
        num = 1
        for thread_ptr in threads:
            gdb.execute(
                "set $zx_threads[%d] = (Thread*) %u" % (num, thread_ptr))
            num += 1

        # Translating gdb values to python often trips over these. Heads up.
        save_print_address = "yes" if gdb.parameter("print address") else "no"
        save_print_symbol = "yes" if gdb.parameter("print symbol") else "no"
        gdb.execute("set print address off")
        gdb.execute("set print symbol off")

        print(
            "%3s %5s %-18s %-32s %s" %
            ("Num", "Pid", "Thread*", "Name", "State"))
        # Make sure we restore these when we're done.
        try:
            user_thread_ptr_t = gdb.lookup_type("UserThread").pointer()
            num = 1
            for thread_ptr in threads:
                # TODO(dje): remove dereference
                _print_thread_summary(
                    thread_ptr.dereference(), num, tls_entry_lkuser,
                    user_thread_ptr_t)
                num += 1
        finally:
            gdb.execute("set print address %s" % (save_print_address))
            gdb.execute("set print symbol %s" % (save_print_symbol))
        if num_threads:
            print("Note: Each thread is now available in $zx_threads[num].")
        else:
            print("<no threads>")


def _print_process_summary(process, number):
    state = str(process["state_"])
    state = state.replace("ProcessDispatcher::", "")
    print("%3d %#16x %4u %s" % (number, process.address, process["id_"], state))


class _InfoZirconProcesses(gdb.Command):
    """info zircon processes command

  This command prints a list of all zircon processes.
  TODO: Allow specifying which processes to print.
  """

    def __init__(self):
        super(_InfoZirconProcesses, self).__init__(
            "info %s processes" % (_ZIRCON_COMMAND_PREFIX), gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        # Do this first to make sure the previous value gets cleared out.
        # There's no way to unset a convenience var, so KISS.
        gdb.execute("set $zx_processes = (ProcessDispatcher*[1]) { 0 }")
        tls_entry_lkuser = gdb.parse_and_eval("TLS_ENTRY_LKUSER")
        processes = _get_process_list()
        num_processes = len(processes)
        # The array is origin-1-indexed. Have a null first entry to KISS.
        gdb.execute(
            "set $zx_processes = (ProcessDispatcher*[%d]) { 0 }" %
            (num_processes + 1))

        # Populate the array first, before printing the summary, to make sure this
        # gets done even if there's an error during printing.
        num = 1
        for process_ptr in processes:
            gdb.execute(
                "set $zx_processes[%d] = (ProcessDispatcher*) %u" %
                (num, process_ptr))
            num += 1

        # Translating gdb values to python often trips over these. Heads up.
        save_print_address = "yes" if gdb.parameter("print address") else "no"
        save_print_symbol = "yes" if gdb.parameter("print symbol") else "no"
        gdb.execute("set print address off")
        gdb.execute("set print symbol off")

        print(
            "%3s %-18s %4s %s" % ("Num", "ProcessDispatcher*", "Pid", "State"))
        # Make sure we restore these when we're done.
        try:
            num = 1
            for process_ptr in processes:
                _print_process_summary(process_ptr.dereference(), num)
                num += 1
        finally:
            gdb.execute("set print address %s" % (save_print_address))
            gdb.execute("set print symbol %s" % (save_print_symbol))
        if num_processes:
            print("Note: Each process is now available in $zx_processes[num].")
        else:
            print("<no processes>")


def _print_handle_summary(handle, number):
    process_id = handle["process_id_"]
    rights = handle["rights_"]
    dispatcher = handle["dispatcher_"]["ptr_"]
    # TODO(dje): This is a hack to get the underlying type from the vtable.
    # The python API should support this directly.
    dispatcher_text = gdb.execute(
        "output *(Dispatcher*) %s" % (dispatcher), to_string=True)
    dispatcher_split_text = dispatcher_text.split(" ", 1)
    if len(dispatcher_split_text) == 2:
        dispatcher_type = dispatcher_split_text[0].strip("()")
    else:
        dispatcher_type = "Dispatcher"
    dispatcher_text = "(%s*) %s" % (dispatcher_type, dispatcher)
    print(
        "  %3d %-18s %4u %#8x %s" %
        (number, handle.address, process_id, rights, dispatcher_text))


class _InfoZirconHandles(gdb.Command):
    """info zircon handles command

  This command prints a list of all zircon handles.
  TODO: Allow specifying which handles to print.
  """

    def __init__(self):
        super(_InfoZirconHandles, self).__init__(
            "info %s handles" % (_ZIRCON_COMMAND_PREFIX), gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        processes = _get_process_list()

        # Translating gdb values to python often trips over these. Heads up.
        save_print_address = "yes" if gdb.parameter("print address") else "no"
        save_print_symbol = "yes" if gdb.parameter("print symbol") else "no"
        gdb.execute("set print address on")
        gdb.execute("set print symbol off")

        # Make sure we restore these when we're done.
        try:
            for p in processes:
                handles = _get_handle_list(p)
                num_handles = len(handles)

                print("Process %u" % (p["id_"]))
                print(
                    "  %3s %-18s %4s %8s %s" %
                    ("Num", "Handle*", "Pid", "Rights", "Dispatcher"))

                num = 1
                for handle_ptr in handles:
                    _print_handle_summary(handle_ptr.dereference(), num)
                    num += 1

                if not num_handles:
                    print("  <no handles>")

        finally:
            gdb.execute("set print address %s" % (save_print_address))
            gdb.execute("set print symbol %s" % (save_print_symbol))


class _ZirconKernelExceptionUnwinder(gdb.Parameter):
    """Parameter to enable zircon kernel exception unwinding.

  This parameter is an escape hatch in case there are problems with the unwinder.

  N.B. Perhaps this command should flush registers.
  It doesn't now to avoid side-effects, and the user is responsible for typing
  "flushregs" if s/he wants to reprint a recent backtrace.
  """

    set_doc = "Set whether the zircon kernel exception unwinder is enabled."
    show_doc = "Show whether the zircon kernel exception unwinder is enabled."

    def __init__(self):
        super(_ZirconKernelExceptionUnwinder, self).__init__(
            "%s %s" %
            (_ZIRCON_COMMAND_PREFIX, _KERNEL_EXCEPTION_UNWINDER_PARAMETER),
            gdb.COMMAND_DATA, gdb.PARAM_BOOLEAN)
        self.value = True

    def get_show_string(self, pvalue):
        value = "enabled" if self.value else "disabled"
        return "The kernel exception unwinder is %s." % (value)

    def get_set_string(self):
        # Ugh.  There doesn't seem to be a way to implement a gdb parameter in
        # Python that will be silent when the user changes the value.
        value = "enabled" if self.value else "disabled"
        return "The kernel exception unwinder is %s." % (value)


class _Amd64KernelExceptionUnwinder(Unwinder):
    # See arch/x86/64/exceptions.S.
    AT_IFRAME_SETUP = "interrupt_common_iframe_set_up_for_debugger"
    INTERRUPT_COMMON = "interrupt_common"

    class FrameId(object):

        def __init__(self, sp, pc):
            self.sp = sp
            self.pc = pc

    @staticmethod
    def lookup_minsym(minsym_name):
        """Return the address of "minimal symbol" minsym_name."""
        # TODO(dje): What's here now is a quick hack to get things going.
        # GDB's python API doesn't yet provide the ability to look up minsyms.
        try:
            output = gdb.execute("output %s" % (minsym_name), to_string=True)
        except (gdb.error):
            return None
        symbol_value = None
        if not output.startswith("No symbol"):
            symbol_match = re.search(r"0x[0-9a-f]+", output)
            if symbol_match is not None:
                symbol_value = long(symbol_match.group(0), 16)
        return symbol_value

    @staticmethod
    def is_user_space(iframe):
        """Return True if iframe is from user space."""
        # See arch/x86/include/arch/x86/descriptor.h:SELECTOR_PL.
        return (iframe["cs"] & 3) != 0

    def __init__(self):
        super(_Amd64KernelExceptionUnwinder,
              self).__init__("AMD64 kernel exception unwinder")
        # We assume uintptr_t is present in the debug info.
        # We *could* use unsigned long, but it's a bit of an obfuscation.
        self.uintptr_t = gdb.lookup_type("uintptr_t")
        # We assume "unsigned int" is 32 bits.
        self.uint32_t = gdb.lookup_type("unsigned int")
        self.iframe_ptr_t = gdb.lookup_type("iframe_t").pointer()
        # We need to know when the pc is at the point where it has called
        # x86_exception_handler.
        self.at_iframe_setup = self.lookup_minsym(
            _Amd64KernelExceptionUnwinder.AT_IFRAME_SETUP)
        self.interrupt_common_begin_addr = self.lookup_minsym(
            _Amd64KernelExceptionUnwinder.INTERRUPT_COMMON)

    def is_in_icommon(self, pc):
        """Return True if pc is in the interrupt_common function."""
        # First do the preferred test.
        # If the pc is here then we've called into x86_exception_handler.
        if self.at_iframe_setup is not None and pc == self.at_iframe_setup:
            return True
        # Fall back to this in case the special symbol doesn't exist.
        if self.interrupt_common_begin_addr is None:
            return False
        end_addr = self.interrupt_common_begin_addr + 64
        return pc >= self.interrupt_common_begin_addr and pc < end_addr

    def __call__(self, pending_frame):
        try:
            # Punt if disabled.
            if not gdb.parameter(
                    "%s %s" %
                (_ZIRCON_COMMAND_PREFIX, _KERNEL_EXCEPTION_UNWINDER_PARAMETER)):
                return None
            # Note: We use rip,rsp here instead of pc,sp to work around bug 20128
            #pc = pending_frame.read_register("pc").cast(self.uintptr_t)
            pc = pending_frame.read_register("rip").cast(self.uintptr_t)
            #print "icommon unwinder, pc = %#x" % (long(str(pc))) # work around bug 20126
            if not self.is_in_icommon(pc):
                return None
            sp = pending_frame.read_register("rsp").cast(self.uintptr_t)
            #print "icommon unwinder, sp = %#x" % (long(str(sp)))
            iframe = sp.cast(self.iframe_ptr_t)
            # This is only for kernel faults, not user-space ones.
            if self.is_user_space(iframe):
                return None
            frame_id = self.FrameId(sp, pc)
            unwind_info = pending_frame.create_unwind_info(frame_id)
            unwind_info.add_saved_register("rip", iframe["ip"])
            unwind_info.add_saved_register("rsp", iframe["user_sp"])
            unwind_info.add_saved_register("rax", iframe["rax"])
            unwind_info.add_saved_register("rbx", iframe["rbx"])
            unwind_info.add_saved_register("rcx", iframe["rcx"])
            unwind_info.add_saved_register("rdx", iframe["rdx"])
            unwind_info.add_saved_register("rbp", iframe["rbp"])
            unwind_info.add_saved_register("rsi", iframe["rsi"])
            unwind_info.add_saved_register("r8", iframe["r8"])
            unwind_info.add_saved_register("r9", iframe["r9"])
            unwind_info.add_saved_register("r10", iframe["r10"])
            unwind_info.add_saved_register("r11", iframe["r11"])
            unwind_info.add_saved_register("r12", iframe["r12"])
            unwind_info.add_saved_register("r13", iframe["r13"])
            unwind_info.add_saved_register("r14", iframe["r14"])
            unwind_info.add_saved_register("r15", iframe["r15"])
            # Flags is recorded as 64 bits, but gdb needs to see 32.
            unwind_info.add_saved_register(
                "eflags", iframe["flags"].cast(self.uint32_t))
            #print "Unwind info:"
            #print unwind_info
            return unwind_info
        except (gdb.error, RuntimeError):
            return None


_ZirconPrefix()
_InfoZircon()
_SetZircon()
_ShowZircon()

_ZirconMaxInfoThreads()
_ZirconMaxInfoProcesses()
_ZirconMaxInfoHandles()

_InfoZirconThreads()
_InfoZirconProcesses()
_InfoZirconHandles()

_ZirconKernelExceptionUnwinder()

_ull = gdb.lookup_type("unsigned long long")
_uint = gdb.lookup_type("unsigned int")


def _cast(value, t, shift=64):
    return int(value.cast(t)) & ((1 << shift) - 1)


def _cast_ull(value):
    """ Cast a value to unsigned long long """
    global _ull
    return _cast(value, _ull)


def _cast_uint(value):
    """ Cast a value to unsigned int"""
    global _uint
    return _cast(value, _uint, 32)


def _read_symbol_address(name):
    """ Read the address of a symbol """
    addr = gdb.parse_and_eval("&" + name)
    try:
        if addr is not None:
            return _cast_ull(addr)
    except gdb.MemoryError:
        pass
    print("Can't find %s to lookup KASLR relocation" % name)
    return None


def _read_pointer(addr):
    """ Read a pointer in an address """
    value = gdb.parse_and_eval("*(unsigned long*)0x%x" % addr)
    try:
        if value is not None:
            return _cast_ull(value)
    except gdb.MemoryError:
        pass

    print("Can't read 0x%x to lookup KASLR ptr value" % addr)
    return None


def _read_uint(addr):
    """ Read a uint """
    value = gdb.parse_and_eval("*(unsigned int*)0x%x" % addr)
    try:
        if value is not None:
            return _cast_uint(value)
    except gdb.MemoryError:
        pass
    print("Can't read 0x%x to lookup KASLR uint value" % addr)
    return None


def _offset_symbols_and_breakpoints(kernel_relocated_base=None):
    """ Using the KASLR relocation address, reload symbols and breakpoints """
    print("Update symbols and breakpoints for KASLR")

    base_address = _read_symbol_address(_KERNEL_BASE_ADDRESS)
    if not base_address:
        return False

    load_start = _read_symbol_address("IMAGE_LOAD_START")
    if not load_start:
        return False

    if not kernel_relocated_base:
        kernel_relocated_base = _read_symbol_address("kernel_relocated_base")
        if not kernel_relocated_base:
            return False

    relocated = _read_pointer(kernel_relocated_base)
    if not relocated:
        print("Failed to fetch KASLR base address")
        return False

    # There is no api for symbol management.
    # Everything has to be done by custom commands
    sym = gdb.execute("info target", to_string=True)
    m = re.match("^Symbols from \"([^\"]+)\"", sym)
    if not m:
        print("Error: Cannot find the target symbol")
        return False
    sym_path = m.group(1)

    # Identify all section addresses
    x = gdb.execute("info target", to_string=True)
    m = re.findall("\s0x([a-f0-9]+) - 0x[a-f0-9]+ is (.*)", x)

    offset = base_address - relocated
    sections = dict([(name, int(addr, 16)) for addr, name in m])
    if len(sections) == 0:
        print("Error: Failed to find sections in binary")
        return False

    if ".text" not in sections:
        print("Error: Failed to find .text section")
        return False

    # Do not prompt the user
    confirm_was_enabled = gdb.parameter("confirm")
    if confirm_was_enabled:
        gdb.execute("set confirm off", to_string=True)

    # Disable auto loading to prevent the script to be reloaded
    auto_loading_enabled = gdb.parameter("auto-load python-scripts")
    if auto_loading_enabled:
        gdb.execute("set auto-load python-scripts off", to_string=True)

    # Remove all symbols
    gdb.execute("symbol-file", to_string=True)

    # Update all addresses to the relocated address
    sections = dict(
        [(name, addr - offset) for name, addr in sections.items()])

    text_addr = sections[".text"]
    del sections[".text"]

    args = ["-s %s 0x%x" % (name, addr) for name, addr in sections.items()]

    # Reload the ELF with all sections set
    gdb.execute("add-symbol-file \"%s\" 0x%x -readnow %s" \
                % (sym_path, text_addr, " ".join(args)), to_string=True)

    if auto_loading_enabled:
        gdb.execute("set auto-load python-scripts on", to_string=True)

    if confirm_was_enabled:
        gdb.execute("set confirm on", to_string=True)

    # Verify it works as expected
    code_start = _read_symbol_address("__code_start")
    if not kernel_relocated_base:
        return False

    expected = relocated + (load_start - base_address)
    if code_start != expected:
        print("Error: Incorrect relocation for __code_start 0x%x vs 0x%x" \
              % (expected, code_start))
        return False

    print("KASLR: Correctly reloaded kernel at 0x%x" % relocated)
    return True


class KASLRBreakpoint(gdb.Breakpoint):
    """ Helper class to setup breakpoints to load symbols for KASLR. """

    def __init__(self):
        self._is_valid = False

    def is_valid(self):
        if gdb.Breakpoint.is_valid(self) == False:
            return False
        return self._is_valid

    def stop(self):
        # A breakpoint cannot change anything so get callback on the following stop
        gdb.events.stop.connect(self._stop_callback_internal)
        return True

    # Callback through events so the state can be changed
    def _stop_callback_internal(self, event):
        gdb.events.stop.disconnect(self._stop_callback_internal)
        self.delete()

        # Call the top callback
        self._stop_callback(event)


class KASLRBootWatchpoint(KASLRBreakpoint):
    """ Watchpoint to catch read access to KASLR relocated address

  The assumption is the address was written before it is read. It is an
  architecture independent way to start at the right moment if the relocated
  KASLR symbol name is the same.
  """

    def __init__(self, pc):
        KASLRBreakpoint.__init__(self)

        base_address = _read_symbol_address(_KERNEL_BASE_ADDRESS)
        if not base_address:
            return

        kernel_relocated_base = _read_symbol_address("kernel_relocated_base")
        if not kernel_relocated_base:
            return

        self._relocated_base_offset = kernel_relocated_base

        if _is_x86_64():
            # x86_64 uses the physical address
            self._relocated_base_offset -= base_address
        elif _is_arm64():
            # Search from pc to find BOOT tag
            found = False
            for addr in range(pc, pc + 0x10000000, 0x10000):
                data = _read_uint(addr)
                if data == _BOOT_MAGIC:
                    self._relocated_base_offset -= base_address
                    self._relocated_base_offset += addr
                    found = True
                    break

            if found == False:
                print("Error: Could not found BOOT_MAGIC")
                return

        self._is_valid = True
        gdb.Breakpoint.__init__(
            self,
            "*0x%x" % self._relocated_base_offset,
            gdb.BP_WATCHPOINT,
            gdb.WP_READ,
            internal=True)

        self.silence = True

    # Callback from KASLRBreakpoint when it is safe to change state
    def _stop_callback(self, event):
        # Load symbols using load_offset
        _offset_symbols_and_breakpoints(self._relocated_base_offset)


class KASLRStartBreakpoint(KASLRBreakpoint):
    """ Breakpoint to catch _start

  The ZBI boot path result with memory being moved before _start on x86_64.
  In this case, break on _start before setting up the watchpoint.
  """

    def __init__(self):
        KASLRBreakpoint.__init__(self)

        base_address = _read_symbol_address(_KERNEL_BASE_ADDRESS)
        if not base_address:
            return

        _start = _read_symbol_address("_start")
        if not _start:
            return

        # Get the physical address
        _start -= base_address

        self._is_valid = True
        gdb.Breakpoint.__init__(self, "*0x%x" % _start, internal=True)
        self.silence = True

    # Callback from KASLRBreakpoint when it is safe to change state
    def _stop_callback(self, event):
        pc = _cast_ull(gdb.parse_and_eval("$pc"))
        x = KASLRBootWatchpoint(pc)
        if not x.is_valid():
            print("Error: Failed create KASLR boot watchpoint after _stat")
            return

        print("Watchpoint set on KASLR relocated base variable")
        gdb.execute("continue")


def _align(addr, shift):
    b64 = (1 << 64) - 1
    mask = (1 << shift) - 1
    return addr & (~mask & b64)


def _identify_offset_to_reload(pc):
    """ From $pc, search the base address to the page size
  Used when attaching to an existing debugging session
  """
    print("Search KASLR base address based on $pc")
    if (pc >> 63) != 1:
        print("Error: Didn't break into kernel code")
        return False

    base_address = _read_symbol_address(_KERNEL_BASE_ADDRESS)
    if not base_address:
        return False

    end = _read_symbol_address("_end")
    if not end:
        return False

    kernel_relocated_base = _read_symbol_address("kernel_relocated_base")
    if not kernel_relocated_base:
        return False

    offset = kernel_relocated_base - base_address
    max_size = end - base_address

    # Search base+offset == base for each previous page until the max size
    found = False
    addr = pc
    while addr > (pc - max_size):
        addr = _align(addr - 1, 12)
        target = addr + offset
        value = _read_pointer(target)
        if value == None:
            break
        if addr == value:
            found = True
            break

    if found == False:
        print("Error: Failed to find the KASLR relocation from the $pc")
        return False

    return _offset_symbols_and_breakpoints(target)


def _is_earlyboot_pc(pc):
    """ Early boot if the top 32-bit is zero """
    return not (pc >> 32)


def _KASLR_stop_event(event):
    """ Called on first stop after debugger started """
    gdb.events.stop.disconnect(_KASLR_stop_event)
    pc = _cast_ull(gdb.parse_and_eval("$pc"))
    if not _is_earlyboot_pc(pc):
        # If not early boot, try to find the kernel base and adapt
        _identify_offset_to_reload(pc)
        return

    x = None

    # ARM64 map _start at random place in memory, directly use the watchpoint
    if _is_arm64():
        x = KASLRBootWatchpoint(pc)
    else:
        x = KASLRStartBreakpoint()

    if not x.is_valid():
        print("Error: Failed create KASLR boot first breakpoint/watchpoint")
        return

    gdb.execute("continue")


def _install():
    current_objfile = gdb.current_objfile()
    # gdb.current_objfile() is only set when autoloading;
    # otherwise we can try to assume the first loaded objfile is the kernel.
    if not current_objfile and gdb.objfiles():
        current_objfile = gdb.objfiles()[0]
    if not current_objfile:
        print("Warning: no object file set")
        return

    register_zircon_pretty_printers(current_objfile)
    if current_objfile is not None and _is_x86_64():
        gdb.unwinder.register_unwinder(
            current_objfile, _Amd64KernelExceptionUnwinder(), True)
        print(
            "Zircon extensions installed for {}".format(
                current_objfile.filename))

    if not _is_x86_64() and not _is_arm64():
        print(
            "Warning: Unsupported architecture, KASLR support will be experimental"
        )
    gdb.events.stop.connect(_KASLR_stop_event)


_install()
