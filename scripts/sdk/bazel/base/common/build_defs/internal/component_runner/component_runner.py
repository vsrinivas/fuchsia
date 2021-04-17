# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re
import shutil
from subprocess import Popen, PIPE
import sys
import tempfile


def run_command(*args, **kwargs):
    no_redirect = kwargs.pop('no_redirect', False)
    output = None if no_redirect else PIPE
    process = Popen(args, stdout=output, stderr=output)
    stdout, stderr = process.communicate()
    if process.returncode:
        if no_redirect:
            raise Exception('Command %s failed' % args)
        else:
            raise Exception('Command %s failed: %s' % (args, stdout + stderr))
    return stdout


def get_target_address(ffx):
    # Find a target device.
    stdout = run_command(ffx, 'target', 'list', '--format', 's')
    match = re.match('^([^\s]+)\s+([^\s]+)$', stdout.strip())
    if not match:
        raise Exception('Could not parse target parameters in %s' % stdout)
    target_address = match.group(1)
    target_name = match.group(2)

    return target_address


def serve_package(pm, package, directory):
    # Set up the package repository.
    run_command(pm, 'newrepo', '-repo', directory)
    run_command(pm, 'publish', '-a', '-r', directory, '-f', package)

    # Start the server.
    server = Popen([pm, 'serve', '-repo', directory], stdout=PIPE, stderr=PIPE)
    return lambda: server.kill()


class MyParser(argparse.ArgumentParser):

    def error(self, message):
        print(
            'Usage: bazel run <package label> -- <component name> --ssh-key '
            '<path to private key>')
        sys.exit(1)


def main():
    parser = MyParser()
    parser.add_argument(
        '--config',
        help='The path to the list of components in the package',
        required=True)
    parser.add_argument(
        '--package-name', help='The name of the Fuchsia package', required=True)
    parser.add_argument(
        '--package', help='The path to the Fuchsia package', required=True)
    parser.add_argument('--ffx', help='The path to the ffx tool', required=True)
    parser.add_argument('--pm', help='The path to the pm tool', required=True)
    subparse = parser.add_subparsers().add_parser('run')
    subparse.add_argument('component', nargs=1)
    subparse.add_argument(
        '--ssh-key',
        help='The absolute path to a private SSH key',
        required=True)
    args = parser.parse_args()

    if not os.path.isabs(args.ssh_key):
        print('Path to SSH key must be absolute, got %s' % args.ssh_key)
        return 1

    with open(args.config, 'r') as config_file:
        components = config_file.readlines()

    component = args.component[0]
    if component not in components:
        print('Error: "%s" not in %s' % (component, components))
        return 1

    staging_dir = tempfile.mkdtemp(prefix='fuchsia-run')

    try:
        target_address = get_target_address(args.ffx)
        stop_server = serve_package(args.pm, args.package, staging_dir)
        try:

            def run_ssh_command(*params, **kwargs):
                base = [
                    'ssh',
                    '-i',
                    args.ssh_key,
                    'fuchsia@' + target_address,
                    '-o',
                    'StrictHostKeyChecking=no',
                    '-o',
                    'UserKnownHostsFile=/dev/null',
                ]
                run_command(*(base + list(params)), **kwargs)

            host_address = run_ssh_command('echo $SSH_CONNECTION')
            if "No devices found" in host_address:
                print(
                    'Error: could not get host address for %s' % target_address)
                return 1
            host_address = host_address.split(' ')[0]
            host_address = host_address.replace('%', '%25')
            server_address = 'http://%s:8083/config.json' % host_address
            run_ssh_command(
                'pkgctl', 'repo', 'add', 'url', '-n', 'devhost', '-f', '1',
                server_address)
            component_uri = "fuchsia-pkg://fuchsia.com/%s#meta/%s.cmx" % (
                args.package_name, component)
            run_ssh_command('run', component_uri, no_redirect=True)
        finally:
            stop_server()
    except Exception as e:
        print(e)
        return 1
    finally:
        shutil.rmtree(staging_dir, ignore_errors=True)

    return 0


if __name__ == '__main__':
    sys.exit(main())
