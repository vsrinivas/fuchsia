#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import glob
import os
import re
import shlex
import subprocess
import time

from host import Host


class Device(object):
    """Represents a Fuchsia device attached to a host.

    This class abstracts the details of remotely running commands and
    transferring data to and from the device.

    Attributes:
      host: A Host object represent the local platform attached to this target
        device.
  """

    @classmethod
    def from_args(cls, host, args):
        """Constructs a Device from command line arguments."""
        netaddr_cmd = ['netaddr', '--fuchsia', '--nowait']
        default_device = '{}.device'.format(host.build_dir)
        if args.device:
            netaddr_cmd.append(args.device)
        elif os.path.exists(default_device):
            with open(default_device) as f:
                netaddr_cmd.append(f.read().strip())
        try:
            netaddr = host.zircon_tool(netaddr_cmd)
        except subprocess.CalledProcessError:
            raise RuntimeError('Unable to find device; try `fx set-device`.')
        device = cls(host, netaddr)
        if not host.build_dir:
            raise Host.ConfigError('Unable to find SSH configuration.')
        device.set_ssh_config(
            Host.join(host.build_dir, 'ssh-keys', 'ssh_config'))
        return device

    def __init__(self, host, addr, port=22):
        self.host = host
        self._addr = addr
        self._ssh_opts = {}
        if port != 22:
            self._ssh_opts['p'] = [str(port)]

    def set_ssh_config(self, config_file):
        """Sets the SSH arguments to use a config file."""
        if not os.path.exists(config_file):
            raise Host.ConfigError('Unable to find SSH configuration.')
        self._ssh_opts['F'] = [config_file]

    def set_ssh_identity(self, identity_file):
        if not os.path.exists(identity_file):
            raise Host.ConfigError('Unable to find SSH identity.')
        self._ssh_opts['i'] = [identity_file]

    def set_ssh_option(self, option):
        """Sets SSH configuration options. Can be used multiple times."""
        if 'o' in self._ssh_opts:
            self._ssh_opts['o'].append(option)
        else:
            self._ssh_opts['o'] = [option]

    def set_ssh_verbosity(self, level):
        """Sets how much debugging SSH prints. Default is 0 (none), max is 3."""
        for i in range(1, 4):
            opt = 'v' * i
            if level == i and not opt in self._ssh_opts:
                self._ssh_opts[opt] = []
            elif level != i and opt in self._ssh_opts:
                del self._ssh_opts[opt]

    def get_ssh_cmd(self, cmd):
        """Returns the SSH executable and options."""
        result = cmd[:1]
        for opt, args in self._ssh_opts.iteritems():
            if len(args) == 0:
                result.append('-' + opt)
            else:
                for arg in args:
                    result.append('-' + opt)
                    result.append(arg)
        return result + cmd[1:]

    def ssh(self, cmdline, **kwargs):
        """Creates a Process with added SSH arguments.

    Provides the additional arguments to handle connecting the device and other
    SSH options. The returned Process represents a command that can be run on
    the remote device.

    Args:
      cmdline: List of command line arguments to execute on device
      kwargs: Same as for subprocess.Popen

    Returns:
      A Process object.
    """
        args = self.get_ssh_cmd(['ssh', self._addr] + cmdline)
        p = self.host.create_process(args, **kwargs)

        # Explicitly prevent the subprocess from inheriting our stdin
        if not p.stdin:
            p.stdin = Host.DEVNULL

        return p

    def getpids(self):
        """Maps names to process IDs for running fuzzers.

    Connects to the device and checks which fuzz targets have a matching entry
    in the component list given by 'cs'.  This matches on *only* the first 32
    characters of the component manifest and package URL.  This is due to 'cs'
    being limited to returning strings of length `ZX_MAX_NAME_LEN`, as defined
    in //zircon/system/public/zircon/types.h.

    Returns:
      A dict mapping fuzz target names to process IDs. May be empty if no
      fuzzers are running.
    """
        out = self.ssh(['cs']).check_output()
        pids = {}
        for fuzzer in self.host.fuzzers:
            tgt = (fuzzer[1] + '.cmx')[:32]
            url = ('fuchsia-pkg://fuchsia.com/%s#meta' % fuzzer[0])[:32]
            for line in str(out).split('\n'):
                match = re.search(tgt + r'\[(\d+)\]: ' + url, line)
                if match:
                    pids[fuzzer[1]] = int(match.group(1))
        return pids

    def ls(self, path):
        """Maps file names to sizes for the given path.

    Connects to a Fuchsia device and lists the files in a directory given by
    the provided path.  Ignore non-existent paths.

    Args:
      path: Absolute path to a directory on the device.

    Returns:
      A dict mapping file names to file sizes, or an empty dict if the path
      does not exist.
    """
        results = {}
        try:
            out = self.ssh(['ls', '-l', path]).check_output()
            for line in str(out).split('\n'):
                # Line ~= '-rw-r--r-- 1 0 0 8192 Mar 18 22:02 some-name'
                parts = line.split()
                if len(parts) > 8:
                    results[' '.join(parts[8:])] = int(parts[4])
        except subprocess.CalledProcessError:
            pass
        return results

    def rm(self, pathname, recursive=False):
        """Removes a file or directory from the device."""
        args = ['rm']
        if recursive:
            args.append('-rf')
        else:
            args.append('-f')
        args.append(pathname)
        self.ssh(args).check_call()

    def dump_log(self, args):
        """Retrieve a syslog from the device."""
        p = self.ssh(
            ['log_listener', '--dump_logs', 'yes', '--pretty', 'no'] + args)
        return p.check_output()

    def guess_pid(self):
        """Tries to guess the fuzzer process ID from the device syslog.

        This will assume the last line which contained one of the strings
        '{{{reset}}}', 'libFuzzer', or 'Sanitizer' is the fuzzer process, and
        try to extract its PID.

        Returns:
          The PID of the process suspected to be the fuzzer, or -1 if no
          suitable candidate was found.
        """
        out = self.dump_log(['--only', 'reset,Fuzzer,Sanitizer'])
        pid = -1
        for line in out.split('\n'):
            # Log lines are like '[timestamp][pid][tid][name] data'
            parts = line.split('][')
            if len(parts) > 2:
                pid = int(parts[1])
        return pid

    def _scp(self, srcs, dst):
        """Copies `src` to `dst`.

    Don't call directly; use `fetch` or `store` instead.`

    Args:
      srcs: Local or remote paths to copy from.
      dst: Local or remote path to copy to.
    """
        args = self.get_ssh_cmd(['scp'] + srcs + [dst])
        p = self.host.create_process(args)
        p.check_call()

    def fetch(self, data_src, host_dst, retries=0):
        """Copies `data_src` on the target to `host_dst` on the host."""
        if not os.path.isdir(host_dst):
            raise ValueError(host_dst + ' is not a directory')
        while retries != 0:
            try:
                self._scp(['[{}]:{}'.format(self._addr, data_src)], host_dst)
                return
            except subprocess.CalledProcessError:
                time.sleep(1)
                retries -= 1
        self._scp(['[{}]:{}'.format(self._addr, data_src)], host_dst)

    def store(self, host_src, data_dst):
        """Copies `host_src` on the host to `data_dst` on the target."""
        self.ssh(['mkdir', '-p', data_dst]).check_call()
        srcs = glob.glob(host_src)
        if len(srcs) == 0:
            return
        self._scp(srcs, '[{}]:{}'.format(self._addr, data_dst))
