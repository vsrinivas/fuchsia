#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from collections import namedtuple
import argparse
import os
import paths
import subprocess
import sys

standard_variant = namedtuple('standard_variant', [
    'is_target',
    'clauses',
])

COMMON_VARIANTS = {
    'asan': standard_variant(True, ['''
    host = false
    variant = "asan"
''']),

    'asan-sancov': standard_variant(True, ['''
    host = false
    variant = "asan-sancov"
''']),

    'ubsan': standard_variant(True, ['''
    host = false
    variant = "ubsan"
''']),

    'ubsan-sancov': standard_variant(True, ['''
    host = false
    variant = "ubsan-sancov"
''']),

    'lto': standard_variant(True, ['''
    host = false
    variant = "lto"
''']),

    'thinlto': standard_variant(True, ['''
    host = false
    variant = "thinlto"
''']),
}

STANDARD_VARIANTS = COMMON_VARIANTS.copy()
STANDARD_VARIANTS.update({
    'host_asan': standard_variant(False, ['''
    # TODO(TO-565): The yasm host tools have leaks.
    # TODO(TO-666): replace futiltiy & cgpt with 1p tools
    host = true
    dir = [ "//third_party/yasm", "//third_party/vboot_reference", "//garnet/tools/vboot_reference" ]
    variant = "asan_no_detect_leaks"
''', '''
    host = true
    variant = "asan"
''']),
})

PARAMETERIZED_VARIANTS = {
    key: value._replace(clauses=[clause + '''
    output_name = %s
'''
                                 for clause in value.clauses])
    for key, value in COMMON_VARIANTS.iteritems()
}

def main():
    parser = argparse.ArgumentParser(
        description="Generate Ninja files for Fuchsia",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument("--args", dest="gn_args", default=[],
                        help="additional args to pass to gn",
                        action="append")
    parser.add_argument("--help-args", dest="gn_args_list",
                        nargs='?', const=True, default=False,
                        help="Show GN build arguments usable in --args",
                        metavar="BUILDARG")
    parser.add_argument("--platforms", "-P",
                        help="comma-separated list of platforms")
    parser.add_argument("--packages", "-p",
                        help="comma separated list of packages",
                        required=True)
    parser.add_argument("--debug",
                        help="generate debug mode build files (default)",
                        dest="build_type",
                        default="debug", action="store_const", const="debug")
    parser.add_argument("--release", "-r",
                        help="generate release mode build files",
                        dest="build_type",
                        action="store_const", const="release")
    parser.add_argument("--build-dir",
                        help="the directory (relative to FUCHSIA_DIR) into which to generate the build")
    parser.add_argument("--ide",
                        help="An IDE value to pass to gn")
    parser.add_argument("--target_cpu", "-t", help="Target CPU",
                        default="x86-64", choices=['x86-64', 'aarch64'])
    parser.add_argument("--goma", help="use goma", metavar="GOMADIR",
                        nargs='?', const=True, default=False)
    parser.add_argument("--ccache", "-c", help="use ccache",
                        action="store_true")
    parser.add_argument("--lto", nargs='?',
                        const='thin', choices=['full', 'thin'],
                        default=None, help="use link time optimization (LTO)")
    parser.add_argument("--thinlto-cache-dir", help="ThinLTO cache directory")
    parser.add_argument("--variant", help="Select standard build variant",
                        action="append", default=[])
    args = parser.parse_args()

    build_dir = os.path.join(paths.FUCHSIA_ROOT,
                             args.build_dir or "out/%s-%s" % (args.build_type,
                                                              args.target_cpu))

    if args.gn_args_list:
        gn_command = ["args", build_dir]
        if isinstance(args.gn_args_list, str):
            gn_command.append("--list=" + args.gn_args_list)
        else:
            gn_command.append("--list")
    else:
        # TODO(TO-734): reenable --check.
        gn_command = ["gen", build_dir]

    cpu_map = {"x86-64":"x64", "aarch64":"arm64"}
    gn_args = [
        'target_cpu="%s"' % cpu_map[args.target_cpu],
        'fuchsia_packages="%s"' % args.packages,
    ]

    if args.build_type == "release":
        gn_args.append("is_debug=false")

    if args.goma:
        gn_args.append("use_goma=true")
        if type(args.goma) is str:
            path = os.path.abspath(args.goma)
            if not os.path.exists(path):
                parser.error('invalid goma path: %s' % path)
            gn_args.append('goma_dir="%s"' % path)
    if args.ccache:
        gn_args.append("use_ccache=true")
    if args.lto:
        gn_args.append("use_lto = true")
        if args.lto == "full":
            gn_args.append("use_thinlto = false")
        elif args.thinlto_cache_dir:
            gn_args.append('thinlto_cache_dir="%s"' % args.thinlto_cache_dir)

    zircon_cpu = {"x86-64": "x86-64", "aarch64": "arm64"}[args.target_cpu]

    if args.platforms:
        gn_args.append('zircon_platforms=[%s]' % ', '.join(
            ['"%s"' % platform for platform in args.platforms.split(',')]))

    select_variants = []
    bad_variants = []
    force_webkit = False
    for variant in args.variant:
        known_variant = STANDARD_VARIANTS.get(variant)
        if known_variant is not None:
            select_variants += known_variant.clauses
            if known_variant.is_target:
                print 'NOTE: Must build WebKit due to --variant %s' % variant
                force_webkit = True
            continue
        variant = variant.split('=', 1)
        if len(variant) == 2:
            known_variant = PARAMETERIZED_VARIANTS.get(variant[0])
            if known_variant is not None:
                names = variant[1].split(',')
                gn_name = '[%s]' % ', '.join('"%s"' % name for name in names)
                select_variants += [(clause % gn_name)
                                    for clause in known_variant.clauses]
                if (known_variant.is_target and
                    ('web_view' in names or 'web_view_test' in names)):
                    print ('NOTE: Must build WebKit due to --variant %s=%s' %
                           (variant[0], variant[1]))
                    force_webkit = True
                continue
        bad_variants.append(variant[0])

    if bad_variants:
        for variant in bad_variants:
            print 'Unrecognized variant %r' % variant
        print 'Try one of: %s' % ', '.join(sorted(STANDARD_VARIANTS.keys()))
        print '        or: %s' % ', '.join(
            name + '=<output_name>,...'
            for name in sorted(PARAMETERIZED_VARIANTS.keys()))
        return 1

    if select_variants:
        gn_args.append('select_variant = [ %s ]' %
                       ', '.join('{ %s }' % clause
                                 for clause in select_variants))

    if force_webkit:
        gn_args.append('use_prebuilt_webkit=false')

    gn_args += args.gn_args

    gn_command.append('--args=' + ' '.join(gn_args))
    if args.ide:
        gn_command.append('--ide=' + args.ide)
    return subprocess.call([paths.GN_PATH] + gn_command)


if __name__ == "__main__":
    sys.exit(main())
