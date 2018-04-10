#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

from argparse import ArgumentParser, FileType
from os import fstat
from struct import pack

# The kernel assumes it is aligned to a 64K boundary
HEADER_SIZE = 65536

def filesize(f):
    if f is None:
        return 0
    try:
        return fstat(f.fileno()).st_size
    except OSError:
        return 0

def parse_int(x):
    return int(x, 0)

def parse_cmdline():
    parser = ArgumentParser()
    parser.add_argument('--kernel', help='path to the kernel', type=FileType('rb'), required=True)
    parser.add_argument('--shim', help='path to the kernel shim', type=FileType('rb'), required=True)
    parser.add_argument('--load_offset', help='kernel load offset', type=parse_int, required=True)
    parser.add_argument('-o', '--output', help='output file name', type=FileType('wb'), required=True)
    return parser.parse_args()

def pad_file(f, padding):
    pad = (padding - (f.tell() & (padding - 1))) & (padding - 1)
    f.write(pack(str(pad) + 'x'))

def main():
    args = parse_cmdline()
    kernel = args.kernel
    shim = args.shim
    out = args.output
    load_offset = args.load_offset
    kernel_size = fstat(kernel.fileno()).st_size + HEADER_SIZE # add 64K for shim

    # write our Linux compatible header
    out.write(pack('I', 0x91005a4d))    # mrs	x19, mpidr_el1 ('MZ' magic)
    out.write(pack('I', 0x14000000 + HEADER_SIZE / 4 - 1))    # branch to end of header
    out.write(pack('Q', load_offset))
    out.write(pack('Q', kernel_size))
    out.write(pack('Q', 0))
    out.write(pack('Q', 0))
    out.write(pack('Q', 0))
    out.write(pack('Q', 0))
    out.write(pack('3s', 'ARM'))
    out.write(pack('B', 0x64))

    pad_file(out, HEADER_SIZE);

    # followed by the shim
    out.write(shim.read())

    pad_file(out, HEADER_SIZE);

    # and finally the kernel
    out.write(kernel.read())

if __name__ == '__main__':
    main()
