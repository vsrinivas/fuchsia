#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import collections
import json
import urllib.parse


def generate_omaha_client_config(configs, key_config):
    packages_by_service_url = collections.defaultdict(list)
    server_by_service_url = {}

    for config in configs:
        package = {}

        url = config['url']
        query = urllib.parse.urlparse(url).query
        if 'hash' in urllib.parse.parse_qs(query):
            raise ValueError(f"pinned URL not allowed: {url}")

        package['url'] = url
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

        service_url = config.get('service_url', None)
        packages_by_service_url[service_url].append(package)

    for service_url in packages_by_service_url:
        if service_url is None:
            continue

        latest = key_config[service_url]['latest']
        historical = key_config[service_url]['historical']

        server_by_service_url[service_url] = {
            'service_url': service_url,
            'public_keys':
                {
                    "latest": {
                        'key': latest['key'],
                        'id': int(latest['id']),
                    },
                    "historical":
                        [
                            {
                                'key': s['key'],
                                'id': int(s['id']),
                            } for s in historical
                        ],
                }
        }

    return {
        'eager_package_configs':
            [
                {
                    "server":
                        server_by_service_url[service_url]
                        if service_url else None,
                    "packages":
                        packages
                } for service_url, packages in packages_by_service_url.items()
            ]
    }


def generate_pkg_resolver_config(configs, key_config):
    del key_config  # unused for now.
    packages = []

    for config in configs:
        package = {}

        package['url'] = config['url']
        if 'executable' in config:
            package['executable'] = config['executable']

        packages.append(package)

    return {'packages': packages}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--out-omaha-client-config",
        type=argparse.FileType('w'),
        help="path to the generated eager package config file for omaha-client",
    )
    parser.add_argument(
        "--out-pkg-resolver-config",
        type=argparse.FileType('w'),
        help="path to the generated eager package config file for pkg-resolver",
    )
    parser.add_argument(
        "--key-config-file",
        type=argparse.FileType('r'),
        help="JSON key config file, with map from service URL to public keys",
    )
    parser.add_argument(
        "eager_package_config_files",
        nargs='+',
        type=argparse.FileType('r'),
        help="JSON config files, one for each eager package",
    )
    args = parser.parse_args()
    key_config = json.load(args.key_config_file)
    configs = [json.load(f) for f in args.eager_package_config_files]

    omaha_client_config = generate_omaha_client_config(configs, key_config)
    json.dump(omaha_client_config, args.out_omaha_client_config, sort_keys=True)

    pkg_resolver_config = generate_pkg_resolver_config(configs, key_config)
    json.dump(pkg_resolver_config, args.out_pkg_resolver_config, sort_keys=True)


if __name__ == "__main__":
    main()
