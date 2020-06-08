#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import errno
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
         host:             The associated Host object for this device.
         addr:             The device's IPv6 address.
         port:             TCP port number that sshd is listening on.
         ssh_config:       Path to an SSH configuration file.
         ssh_identity:     Path to an SSH identity file.
         ssh_options:      SSH options as in an SSH configuration file.
         ssh_verbosity:    How verbose SSH processes are, from 0 to 3.
    """

    @classmethod
    def from_host(cls, host):
        """Constructs a Device from a Host object."""
        addr = None
        try:
            with open('{}.device'.format(host.build_dir)) as opened:
                addr = host.find_device(device_file=opened)
        except IOError as e:
            if e.errno != errno.ENOENT:
                raise
        if not addr:
            addr = host.find_device()
        device = cls(host, addr)
        device.configure()
        return device

    def __init__(self, host, addr):
        self._host = host
        self._addr = addr
        self._ssh_options = {}
        self._ssh_config_options = []
        self._ssh_verbosity = 0
        self._pids = None

    @property
    def host(self):
        """The associated Host object for this device."""
        return self._host

    @property
    def addr(self):
        """The device's IPv6 address."""
        return self._addr

    @property
    def port(self):
        """The TCP port number that the device's sshd is listening on."""
        return int(self._ssh_options.get('P', '22'))

    @port.setter
    def port(self, port):
        self._ssh_options['P'] = str(port)

    @property
    def ssh_config(self):
        """Path to an SSH configuration file."""
        return self._ssh_options.get('F', None)

    @ssh_config.setter
    def ssh_config(self, ssh_config):
        if not self.host.isfile(ssh_config):
            raise ValueError(
                'Invalid SSH configuration file: {}'.format(ssh_config))
        self._ssh_options['F'] = ssh_config

    @property
    def ssh_identity(self):
        """Path to an SSH identity file."""
        return self._ssh_options.get('i', None)

    @ssh_identity.setter
    def ssh_identity(self, ssh_identity):
        if not self.host.isfile(ssh_identity):
            raise ValueError(
                'Invalid SSH identity file: {}'.format(ssh_identity))
        self._ssh_options['i'] = ssh_identity

    @property
    def ssh_options(self):
        """SSH configuration options, as in an SSH configuration file."""
        return self._ssh_config_options

    @ssh_options.setter
    def ssh_options(self, ssh_options):
        self._ssh_config_options = ssh_options

    @property
    def ssh_verbosity(self):
        """How verbose SSH processes are, from 0 to 3."""
        return self._ssh_verbosity

    @ssh_verbosity.setter
    def ssh_verbosity(self, ssh_verbosity):
        if ssh_verbosity < 0 or ssh_verbosity > 3:
            raise ValueError('Invalid ssh_verbosity: {}'.format(ssh_verbosity))
        self._ssh_verbosity = ssh_verbosity

    def configure(self):
        """Sets the defaults for this device."""
        self.ssh_config = self.host.fxpath(
            self.host.build_dir, 'ssh-keys', 'ssh_config')

    def _ssh_opts(self):
        """Returns the SSH executable and options."""
        ssh_options = []

        # Flags
        if self._ssh_verbosity != 0:
            ssh_options += ['-{}'.format('v' * self._ssh_verbosity)]

        # Options
        for key, val in sorted(self._ssh_options.iteritems()):
            ssh_options += ['-{}'.format(key), val]

        # Configuration options
        for val in sorted(self._ssh_config_options):
            ssh_options += ['-o', val]

        return ssh_options

    def _ssh_cmd(self, args):
        """Returns the command line arguments for an SSH commaned."""
        return ['ssh'] + self._ssh_opts() + [self.addr] + args

    def ssh(self, args, **kwargs):
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
        cmd = self._ssh_cmd(args)
        p = self.host.create_process(cmd, **kwargs)

        # Explicitly prevent the subprocess from inheriting our stdin
        if not p.stdin:
            p.stdin = Host.DEVNULL

        return p

    def _cs_cmd(self):
        """Returns the command to list running components on a device."""
        return ['cs']

    def _cs_url(self, package, executable):
        """Returns the Fuchsia URL for a packaged executable."""
        return (
            'fuchsia-pkg://fuchsia.com/{}#meta/{}'.format(
                package, self._cs_cmx(executable)))

    def _cs_cmx(self, executable):
        """Returns the component manifest name for an executable."""
        return ('{}.cmx'.format(executable))

    def getpid(self, package, executable, refresh=False):
        """Returns a process ID for a packaged executable if running, or -1.

           Relative to the duration of most "fx fuzz" commands, the SSH
           invocation to get component status is fairly expensive. Most
           prcoesses won't meaningfully change during or command (or will only
           change as a result of it). Thus, it makes sense to generally cache
           the PID results. This can lead to small degree of inaccuracy, e.g.
           "fx fuzz check" reporting a fuzzer as "RUNNING" when it stops
           between the command invocation and the display of results. This is
           unlikely (and inconseuqential) enough in normal operation to be
           deemed acceptable.

           If an accurate status is needed, e.g. as part of a long-lived command
           like Fuzzer.monitor(), "refresh" can be set to True to re0run the SSH
           command.
        """
        if self._pids == None or refresh:
            self._pids = {}
            out = self.ssh(self._cs_cmd()).check_output()
            pat = re.compile(
                r'  (?P<executable>.*)\.cmx\[(?P<pid>\d+)\]: ' +
                r'fuchsia-pkg://fuchsia.com/(?P<package>.*)#meta/(?P=executable).cmx'
            )
            for line in str(out).split('\n'):
                match = re.match(pat, line)
                if not match:
                    continue
                groupdict = match.groupdict()
                nametuple = (groupdict['package'], groupdict['executable'])
                pid = int(groupdict['pid'])
                self._pids[nametuple] = pid
        return self._pids.get((package, executable), -1)

    def _ls_cmd(self, path):
        """Returns a command to list a path on device."""
        return ['ls', '-l', path]

    def ls(self, path):
        """Returns a map of file names to sizes for the given path."""
        results = {}
        try:
            out = self.ssh(self._ls_cmd(path)).check_output()
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

    def _rpath(self, pathname):
        """Returns an scp-style pathname argument for a remote path."""
        return '[{}]:{}'.format(self.addr, pathname)

    def _scp_cmd(self, args):
        """Returns the command line arguments for an SSH commaned."""
        return ['scp'] + self._ssh_opts() + sorted(args[:-1]) + [args[-1]]

    def fetch(self, data_src, host_dst, retries=0, delay_ms=1000):
        """Copies `data_src` on the target to `host_dst` on the host."""
        if not self.host.isdir(host_dst):
            raise ValueError(host_dst + ' is not a directory')
        cmd = self._scp_cmd([self._rpath(data_src), host_dst])
        while retries != 0:
            try:
                self.host.create_process(cmd).check_call()
                return
            except subprocess.CalledProcessError:
                time.sleep(delay_ms * 0.001)
                retries -= 1
        self.host.create_process(cmd).check_call()

    def store(self, host_src, data_dst):
        """Copies `host_src` on the host to `data_dst` on the target."""
        self.ssh(['mkdir', '-p', data_dst]).check_call()
        srcs = self.host.glob(host_src)
        if len(srcs) == 0:
            return
        cmd = self._scp_cmd(srcs + [self._rpath(data_dst)])
        self.host.create_process(cmd).check_call()
