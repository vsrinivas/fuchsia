# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import fcntl
import os
import re
import subprocess
import sys
import termios
import time

LINUX_BIN_WIRESHARK = u"wireshark"
LINUX_BIN_TSHARK = u"tshark"
TARGET_TMP_DIR = u"/tmp/"


def has_cmd(binary_name):
    return any(
        os.access(os.path.join(path, binary_name), os.X_OK)
        for path in os.environ["PATH"].split(os.pathsep))


def has_wireshark_env():
    """Test if wireshark GUI can run.

    Returns:
      (True, "") if wireshark can run in the environment.
      (False, Error_String) otherwise.
    """
    platform = os.uname()
    print(platform)
    if not platform:
        return False, u"Failed to get uname"
    if platform[0].lower() != u"linux":
        return False, u"Supported only on Linux"

    if not has_cmd(LINUX_BIN_WIRESHARK):
        return False, u"can\'t find %s" % LINUX_BIN_WIRESHARK

    # All look good.
    return True, u""


def run_cmd(cmd):
    """Run subprocess.run() safely and returns the stdout.

    Args:
      cmd: a command line string.

    Returns:
      The stdout outcome of the shell command.
    """
    result = subprocess.run(
        cmd.split(), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return result.stdout.decode()


def can_run_cmd(cmd):
    """Test if the environment can run cmd.

    Args:
      cmd: a command line string.

    Returns:
      True if the command can run without error.
      False otherwise.
    """
    try:
        subprocess.check_call(
            cmd.split(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return True
    except subprocess.CalledProcessError:
        return False


def invoke_shell(cmd):
    """invoke_shell() uses shell=True to support the command
    that includes shell escape sequences.

    Args:
      cmd: command string, which may include escape sequence.
    """
    print(u"Invoke shell:" + cmd)
    p = subprocess.Popen(cmd, shell=True)
    p.wait()


def get_interface_names():
    """Get all network interface names of the target except for the loopback.

    Returns:
      A list of interface names.
    """
    result = run_cmd(u"fx shell tcpdump --list-interfaces")

    names = []
    for line in result.split(u"\n"):
        if not line:
            break

        names.append(re.match("^\d+\.([\w-]+)\s", line).groups()[0])

    return names


def has_fuzzy_name(name_under_test, names):
    if not name_under_test:
        return False
    for n in names:
        if name_under_test in n:
            return True
    return False


def build_cmd(args):
    """Build cmd line for sniffing and displaying.

    Args:
      args: command line arguments.

    Returns:
      cmd string.
    """

    fx_workflow_filter = (
        u' "not ('
        u'port ssh or dst port 8083 or dst port 2345 or port 1900 '
        u'or (ip6 and (dst portrange 33330-33341 or dst portrange 33337-33338))'
        u')"')

    # Pay special attention to the escape sequence
    # This command goes through the host shell, and ssh shell.
    cmd_prefix = u"tcpdump -l --packet-buffered -i \"%s\" --no-promiscuous-mode " % (
        args.interface_name)
    cmd_suffix = fx_workflow_filter
    cmd_options = u""
    host_cmd = u""

    output_file = None
    if args.file:
        output_file = u"%s%s" % (TARGET_TMP_DIR, args.file)

    # Build more options
    if args.wireshark:
        (result, err_str) = has_wireshark_env()
        if not result:
            msg = (
                u"Does not have a working wireshark envirionment. "
                u"Note it requires graphical environment "
                u"such as X Display: %s" % err_str)
            print(msg)
            return
        cmd_options += u"-w -"
        if output_file:
            cmd_suffix += u" | tee %s" % output_file
        host_cmd += u" | wireshark -k -i -"
    elif output_file:
        cmd_options += u"-w %s " % output_file

    cmd = u"fx shell '" + cmd_prefix + cmd_options + cmd_suffix + u"'" + host_cmd

    if args.timeout:
        cmd = ("timeout %ss " % args.timeout) + cmd

    return cmd


def get_keystroke_unblocking():
    """Returns a keystroke in a non-blocking way.
    """

    fd = sys.stdin.fileno()

    attr_org = termios.tcgetattr(fd)
    flags_org = fcntl.fcntl(fd, fcntl.F_GETFL)

    attr_new = attr_org[::]
    attr_new[3] = attr_new[3] & ~termios.ICANON & ~termios.ECHO
    flags_new = flags_org | os.O_NONBLOCK

    try:
        termios.tcsetattr(fd, termios.TCSANOW, attr_new)
        fcntl.fcntl(fd, fcntl.F_SETFL, flags_new)
        return sys.stdin.read(1)
    finally:
        termios.tcsetattr(fd, termios.TCSAFLUSH, attr_org)
        fcntl.fcntl(fd, fcntl.F_SETFL, flags_org)


def do_sniff(cmd):
    """Run user-interruptible sniffer.

    Args:
      cmd: command string, which may include escape sequence.
    """
    print("Run: {}".format(cmd))
    p = subprocess.Popen(cmd, shell=True)
    while p.poll() is None:
        time.sleep(0.07)  # To tame the CPU cycle consumption
        user_key = get_keystroke_unblocking()
        if user_key in ["q", "c", "Q", "C"]:
            print(" ... forced stop by user ({})".format(user_key))
            run_cmd("fx shell killall tcpdump")
            p.terminate()


def move_out_dumpfile(filename):
    """Move the PCAPNG dump file from the target device to the host device.

    Args:
      filename: filename stored in the target. Empty string if no file was stored.
    """
    if not filename:
        return

    full_path = u"%s%s" % (TARGET_TMP_DIR, filename)
    cmd = u"cd %s" % os.environ[u"FUCHSIA_OUT_DIR"]
    cmd += u"; fx scp \"[$(fx get-device-addr)]:%s\" ." % full_path
    cmd += u"; fx shell rm -rf %s" % full_path
    invoke_shell(cmd)


def is_target_ready():
    """Tests if the target Fuchsia device is ready to capture packets.

    Returns:
      True if the target is ready. False otherwise.
    """
    if not can_run_cmd("fx shell exit"):
        print("failed to run: the target device unreachable by 'fx shell'")
        return False
    if not can_run_cmd("fx shell which tcpdump"):
        msg = (
            "failed to run: the target does not have 'tcpdump'. "
            "Build with '--with-base //third_party/tcpdump' "
            "and reload the target")
        print(msg)
        return False
    return True


def main():
    if not is_target_ready():
        sys.exit(1)

    iface_names = get_interface_names()
    iface_name_help = "Choose one interface name from: " + ", ".join(
        iface_names)

    parser = argparse.ArgumentParser(
        description=u"Capture packets on the target, Display on the host")

    parser.add_argument(
        u"interface_name", nargs=u"?", default=u"", help=iface_name_help)
    parser.add_argument(
        u"-t", u"--timeout", default=30, help=u"Time duration to sniff")
    parser.add_argument(
        u"--wireshark", action="store_true", help=u"Display on Wireshark.")
    parser.add_argument(
        u"--file",
        type=str,
        default=u"",
        help=
        u"Store PCAPNG file in //out directory. May use with --wireshark option"
    )

    args = parser.parse_args()

    # Sanitize the file name
    if args.file:
        if not args.file.endswith(u".pcapng"):
            args.file = args.file + ".pcapng"

    if not has_fuzzy_name(args.interface_name, iface_names):
        print(iface_name_help)
        sys.exit(1)

    do_sniff(build_cmd(args))
    print(u"\nEnd of fx sniff")

    move_out_dumpfile(args.file)
    sys.exit(0)


if __name__ == u"__main__":
    sys.exit(main())
