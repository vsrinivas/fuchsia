# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# GDB support for magenta kernel.

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
_MAGENTA_COMMAND_PREFIX = "magenta"
_KERNEL_EXCEPTION_UNWINDER_PARAMETER = "kernel-exception-unwinder"

_THREAD_MAGIC = 0x74687264

print("Loading magenta.elf-gdb.py ...")


def _is_x86_64():
  """Return True if we're on an x86-64 platform."""
  # TODO(dje): gdb doesn't provide us with a simple way to do this.
  arch_text = gdb.execute("show architecture", to_string=True)
  return re.search(r"x86-64", arch_text)


# The default is 2 seconds which is too low.
# But don't override something the user set.
# [If the user set it to the default, too bad. :-)]
# TODO(dje): Alas this trips over upstream PR 20084.
#_DEFAULT_GDB_REMOTETIMEOUT = 2
#if int(gdb.parameter("remotetimeout")) == _DEFAULT_GDB_REMOTETIMEOUT:
#  gdb.execute("set remotetimeout 10")


class _MagentaPrefix(gdb.Command):
  """magenta command prefix"""

  def __init__(self):
    super(_MagentaPrefix, self).__init__("%s" % (_MAGENTA_COMMAND_PREFIX),
                                        gdb.COMMAND_DATA,
                                        prefix=True)


class _InfoMagenta(gdb.Command):
  """info magenta command prefix"""

  def __init__(self):
    super(_InfoMagenta, self).__init__("info %s" % (_MAGENTA_COMMAND_PREFIX),
                                       gdb.COMMAND_DATA,
                                       prefix=True)


class _SetMagenta(gdb.Command):
  """set magenta command prefix"""

  def __init__(self):
    super(_SetMagenta, self).__init__("set %s" % (_MAGENTA_COMMAND_PREFIX),
                                      gdb.COMMAND_DATA,
                                      prefix=True)


class _ShowMagenta(gdb.Command):
  """show magenta command prefix"""

  def __init__(self):
    super(_ShowMagenta, self).__init__("show %s" % (_MAGENTA_COMMAND_PREFIX),
                                       gdb.COMMAND_DATA,
                                       prefix=True)


class _MagentaMaxInfoThreads(gdb.Parameter):
  """Parameter to limit output of "info magenta threads" command.

  This parameter is an escape hatch to catch corrupted lists.
  We don't want "info magenta threads" to loop forever.

  The value is the maximum number of threads that will be printed.
  """

  set_doc = "Set the maximum number of magenta threads to print."
  show_doc = "Show the maximum number of magenta threads to print."

  def __init__(self):
    super(_MagentaMaxInfoThreads, self).__init__(
        "%s max-info-threads" % (_MAGENTA_COMMAND_PREFIX),
        gdb.COMMAND_DATA, gdb.PARAM_UINTEGER)
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


class _MagentaMaxInfoProcesses(gdb.Parameter):
  """Parameter to limit output of "info magenta processes" command.

  This parameter is an escape hatch to catch corrupted lists.
  We don't want "info magenta processes" to loop forever.

  The value is the maximum number of processes that will be printed.
  """

  set_doc = "Set the maximum number of magenta processes to print."
  show_doc = "Show the maximum number of magenta processes to print."

  def __init__(self):
    super(_MagentaMaxInfoProcesses, self).__init__(
        "%s max-info-processes" % (_MAGENTA_COMMAND_PREFIX),
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


class _MagentaMaxInfoHandles(gdb.Parameter):
  """Parameter to limit output of "info magenta handles" command.

  This parameter is an escape hatch to catch corrupted lists.
  We don't want "info magenta handles" to loop forever.

  The value is the maximum number of handles that will be printed.
  """

  set_doc = "Set the maximum number of magenta handles to print."
  show_doc = "Show the maximum number of magenta handles to print."

  def __init__(self):
    super(_MagentaMaxInfoHandles, self).__init__(
        "%s max-info-handles" % (_MAGENTA_COMMAND_PREFIX),
        gdb.COMMAND_DATA, gdb.PARAM_UINTEGER)
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
  """Python version of magenta's containerof macro."""
  # TODO(dje): This could only be computed once.
  # For more popular types, compute all possible once.
  char_ptr = gdb.lookup_type("char").pointer()
  ptr = node_ptr.cast(char_ptr)
  type_object_ptr = gdb.lookup_type(type_name).pointer()
  offsetof = long(gdb.Value(0).cast(type_object_ptr)[member_name].address)
  return (ptr - offsetof).cast(type_object_ptr)


def _build_magenta_pretty_printers():
  pp = gdb.printing.RegexpCollectionPrettyPrinter("magenta")
  # Insert printer registration here.
  #pp.add_printer("foo", "^foo$", _MagentaFooPrinter)
  return pp


def register_magenta_pretty_printers(obj):
  if obj is None:
    obj = gdb
  gdb.printing.register_pretty_printer(obj, _build_magenta_pretty_printers(),
                                       replace=True)


def _get_thread_list():
  """ Return a list of all thread_t threads.

  The result is constrained by "magenta max-info-threads".
  """
  threads = []
  head = gdb.parse_and_eval("&thread_list")
  t = head["next"]
  count = 0
  max_threads = gdb.parameter("%s max-info-threads" % (_MAGENTA_COMMAND_PREFIX))
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

  The result is constrained by "magenta max-info-processes".
  """
  processes = []
  head = gdb.parse_and_eval("&process_list")
  head = head["head_"]
  p = head
  count = 0
  max_processes = gdb.parameter("%s max-info-processes" % (_MAGENTA_COMMAND_PREFIX))
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

  The result is constrained by "magenta max-info-handles".
  """
  handles = []
  head = process["handles_"]
  head = head["head_"]
  h = head
  count = 0
  max_handles = gdb.parameter("%s max-info-handles" % (_MAGENTA_COMMAND_PREFIX))
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
  print("%3d %5s %#16x %-32s %s" % (
      number, pid, thread.address, name, thread["state"]))


class _InfoMagentaThreads(gdb.Command):
  """info magenta threads command

  This command prints a list of all magenta threads.
  TODO: Allow specifying which threads to print.
  """

  def __init__(self):
    super(_InfoMagentaThreads, self).__init__("info %s threads" % (_MAGENTA_COMMAND_PREFIX),
                                              gdb.COMMAND_USER)

  def invoke(self, arg, from_tty):
    # Do this first to make sure the previous value gets cleared out.
    # There's no way to unset a convenience var, so KISS.
    gdb.execute("set $mx_threads = (thread_t*[1]) { 0 }")
    tls_entry_lkuser = gdb.parse_and_eval("TLS_ENTRY_LKUSER")
    threads = _get_thread_list()
    num_threads = len(threads)
    # The array is origin-1-indexed. Have a null first entry to KISS.
    gdb.execute("set $mx_threads = (thread_t*[%d]) { 0 }" % (num_threads + 1))

    # Populate the array first, before printing the summary, to make sure this
    # gets done even if there's an error during printing.
    num = 1
    for thread_ptr in threads:
      gdb.execute("set $mx_threads[%d] = (thread_t*) %u" % (num, thread_ptr))
      num += 1

    # Translating gdb values to python often trips over these. Heads up.
    save_print_address = "yes" if gdb.parameter("print address") else "no"
    save_print_symbol = "yes" if gdb.parameter("print symbol") else "no"
    gdb.execute("set print address off")
    gdb.execute("set print symbol off")

    print("%3s %5s %-18s %-32s %s" % (
        "Num", "Pid", "thread_t*", "Name", "State"))
    # Make sure we restore these when we're done.
    try:
      user_thread_ptr_t = gdb.lookup_type("UserThread").pointer()
      num = 1
      for thread_ptr in threads:
        # TODO(dje): remove dereference
        _print_thread_summary(thread_ptr.dereference(), num, tls_entry_lkuser, user_thread_ptr_t)
        num += 1
    finally:
      gdb.execute("set print address %s" % (save_print_address))
      gdb.execute("set print symbol %s" % (save_print_symbol))
    if num_threads:
      print("Note: Each thread is now available in $mx_threads[num].")
    else:
      print("<no threads>")


def _print_process_summary(process, number):
  state = str(process["state_"])
  state = state.replace("ProcessDispatcher::", "")
  print("%3d %#16x %4u %s" % (
      number, process.address, process["id_"], state))


class _InfoMagentaProcesses(gdb.Command):
  """info magenta processes command

  This command prints a list of all magenta processes.
  TODO: Allow specifying which processes to print.
  """

  def __init__(self):
    super(_InfoMagentaProcesses, self).__init__("info %s processes" % (_MAGENTA_COMMAND_PREFIX),
                                              gdb.COMMAND_USER)

  def invoke(self, arg, from_tty):
    # Do this first to make sure the previous value gets cleared out.
    # There's no way to unset a convenience var, so KISS.
    gdb.execute("set $mx_processes = (ProcessDispatcher*[1]) { 0 }")
    tls_entry_lkuser = gdb.parse_and_eval("TLS_ENTRY_LKUSER")
    processes = _get_process_list()
    num_processes = len(processes)
    # The array is origin-1-indexed. Have a null first entry to KISS.
    gdb.execute("set $mx_processes = (ProcessDispatcher*[%d]) { 0 }" % (num_processes + 1))

    # Populate the array first, before printing the summary, to make sure this
    # gets done even if there's an error during printing.
    num = 1
    for process_ptr in processes:
      gdb.execute("set $mx_processes[%d] = (ProcessDispatcher*) %u" % (num, process_ptr))
      num += 1

    # Translating gdb values to python often trips over these. Heads up.
    save_print_address = "yes" if gdb.parameter("print address") else "no"
    save_print_symbol = "yes" if gdb.parameter("print symbol") else "no"
    gdb.execute("set print address off")
    gdb.execute("set print symbol off")

    print("%3s %-18s %4s %s" % (
        "Num", "ProcessDispatcher*", "Pid", "State"))
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
      print("Note: Each process is now available in $mx_processes[num].")
    else:
      print("<no processes>")


def _print_handle_summary(handle, number):
  process_id = handle["process_id_"]
  rights = handle["rights_"]
  dispatcher = handle["dispatcher_"]["ptr_"]
  # TODO(dje): This is a hack to get the underlying type from the vtable.
  # The python API should support this directly.
  dispatcher_text = gdb.execute("output *(Dispatcher*) %s" % (dispatcher), to_string=True)
  dispatcher_split_text = dispatcher_text.split(" ", 1)
  if len(dispatcher_split_text) == 2:
    dispatcher_type = dispatcher_split_text[0].strip("()")
  else:
    dispatcher_type = "Dispatcher"
  dispatcher_text = "(%s*) %s" % (dispatcher_type, dispatcher)
  print("  %3d %-18s %4u %#8x %s" % (
      number, handle.address, process_id, rights, dispatcher_text))


class _InfoMagentaHandles(gdb.Command):
  """info magenta handles command

  This command prints a list of all magenta handles.
  TODO: Allow specifying which handles to print.
  """

  def __init__(self):
    super(_InfoMagentaHandles, self).__init__("info %s handles" % (_MAGENTA_COMMAND_PREFIX),
                                              gdb.COMMAND_USER)

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
        print("  %3s %-18s %4s %8s %s" % (
            "Num", "Handle*", "Pid", "Rights", "Dispatcher"))

        num = 1
        for handle_ptr in handles:
          _print_handle_summary(handle_ptr.dereference(), num)
          num += 1

        if not num_handles:
          print("  <no handles>")

    finally:
      gdb.execute("set print address %s" % (save_print_address))
      gdb.execute("set print symbol %s" % (save_print_symbol))


class _MagentaKernelExceptionUnwinder(gdb.Parameter):
  """Parameter to enable magenta kernel exception unwinding.

  This parameter is an escape hatch in case there are problems with the unwinder.

  N.B. Perhaps this command should flush registers.
  It doesn't now to avoid side-effects, and the user is responsible for typing
  "flushregs" if s/he wants to reprint a recent backtrace.
  """

  set_doc = "Set whether the magenta kernel exception unwinder is enabled."
  show_doc = "Show whether the magenta kernel exception unwinder is enabled."

  def __init__(self):
    super(_MagentaKernelExceptionUnwinder, self).__init__(
        "%s %s" % (_MAGENTA_COMMAND_PREFIX, _KERNEL_EXCEPTION_UNWINDER_PARAMETER),
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
    super(_Amd64KernelExceptionUnwinder, self).__init__("AMD64 kernel exception unwinder")
    # We assume uintptr_t is present in the debug info.
    # We *could* use unsigned long, but it's a bit of an obfuscation.
    self.uintptr_t = gdb.lookup_type("uintptr_t")
    # We assume "unsigned int" is 32 bits.
    self.uint32_t = gdb.lookup_type("unsigned int")
    self.iframe_ptr_t = gdb.lookup_type("x86_iframe_t").pointer()
    # We need to know when the pc is at the point where it has called
    # x86_exception_handler.
    self.at_iframe_setup = self.lookup_minsym(_Amd64KernelExceptionUnwinder.AT_IFRAME_SETUP)
    self.interrupt_common_begin_addr = self.lookup_minsym(_Amd64KernelExceptionUnwinder.INTERRUPT_COMMON)

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
      if not gdb.parameter("%s %s" % (_MAGENTA_COMMAND_PREFIX, _KERNEL_EXCEPTION_UNWINDER_PARAMETER)):
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
      unwind_info.add_saved_register("eflags", iframe["flags"].cast(self.uint32_t))
      #print "Unwind info:"
      #print unwind_info
      return unwind_info
    except (gdb.error, RuntimeError):
      return None


_MagentaPrefix()
_InfoMagenta()
_SetMagenta()
_ShowMagenta()

_MagentaMaxInfoThreads()
_MagentaMaxInfoProcesses()
_MagentaMaxInfoHandles()

_InfoMagentaThreads()
_InfoMagentaProcesses()
_InfoMagentaHandles()

_MagentaKernelExceptionUnwinder()

def _install():
  current_objfile = gdb.current_objfile()
  register_magenta_pretty_printers(current_objfile)
  if current_objfile is not None and _is_x86_64():
    gdb.unwinder.register_unwinder(current_objfile, _Amd64KernelExceptionUnwinder(), True)

_install()
