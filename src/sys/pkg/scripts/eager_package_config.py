#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json


def generate_omaha_client_config(configs):
    packages = []

    for config in configs:
        package = {}

        package['url'] = config['url']
        if 'flavor' in config:
            package['flavor'] = config['flavor']

        channel_config = {'channels': []}
        if 'default_channel' in config:
            default_channel = config['default_channel']

            assert any(
                default_channel in realm['channels']
                for realm in config['realms'])

            channel_config['default_channel'] = default_channel

        for realm in config['realms']:
            for channel in realm['channels']:
                channel_config['channels'].append(
                    {
                        'name': channel,
                        'repo': channel,
                        'appid': realm['app_id'],
                    })

        package['channel_config'] = channel_config

        packages.append(package)

    return {'packages': packages}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--out-omaha-client-config",
        type=argparse.FileType('w'),
        help="path to the generated eager package config file for omaha client",
    )
    parser.add_argument(
        "eager_package_config_files",
        nargs='+',
        type=argparse.FileType('r'),
        help="JSON config files, one for each eager package",
    )
    args = parser.parse_args()
    configs = [json.load(f) for f in args.eager_package_config_files]

    omaha_client_config = generate_omaha_client_config(configs)
    json.dump(omaha_client_config, args.out_omaha_client_config, sort_keys=True)


if __name__ == "__main__":
    main()
