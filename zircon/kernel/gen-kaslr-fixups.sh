#!/usr/bin/env bash

# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

#
# This script examines the relocations left in the kernel ELF image
# by the linker's --emit-relocs feature to find the boot-time data
# fixups that need to be done for KASLR.  The output is a series of
# assembly lines that look like this:
#     fixup 0xADDR, COUNT, STRIDE
# This says that at virtual address 0xADDR there is a naturally aligned
# address (64-bit) word that needs to be adjusted.  This starts a run of
# COUNT addresses separated by STRIDE bytes (so STRIDE=8 if contiguous).
# Both 0xADDR and the address value it points to are "link-time absolute",
# meaning the first byte of the kernel image has the address that the ELF
# symbol __code_start says.  Each address word is incremented by the
# difference between the chosen run-time virtual address of the kernel
# and the link-time __code_start value.
#
# kernel/arch/CPU/image.S implements the `fixup` assembly macro to apply
# each run of adjustments, and then #include's the output of this script.

usage() {
  echo >&2 "Usage: $0 READELF OBJDUMP [--pure] KERNEL OUTFILE [DEPFILE]"
  exit 2
}

if [ $# -lt 2 ]; then
  usage
fi

READELF="$1"
shift
OBJDUMP="$1"
shift

PURE=0
if [ $# -gt 1 -a "$1" = --pure ]; then
  PURE=1
  shift
fi

if [ $# -ne 2 -a $# -ne 3 ]; then
  usage
fi

AWK=awk
KERNEL="$1"
OUTFILE="$2"
DEPFILE="$3"

grok_fixups() {
  if [[ "$("$AWK" --version 2>&1)" == GNU* ]]; then
      # This is just asort(relocs) in GNU awk; POSIX awk has no such function.
      sort_relocs='asort(relocs)'
  else
      # Bubble sort ftw.  Unreasonably slow when there are too many relocs.
      # Sorting is only an optimization to find runs, so punt when the build
      # would take forever.  This should only happen in strange builds like
      # instrumented ones and if it's happening things are not optimized well
      # so making the fixup code a bit smaller is the least of the troubles.
      sort_relocs='
      for (n = 1; n < nrelocs && nrelocs < 5000; ++n) {
          for (i = 1; i <= nrelocs - 1; ++i) {
              if (relocs[i] > relocs[i + 1]) {
                  tmp = relocs[i];
                  relocs[i] = relocs[i + 1];
                  relocs[i + 1] = tmp;
              }
          }
      }'
  fi

  "$AWK" -v kernel="$KERNEL" -v objdump="$OBJDUMP" -v pure=$PURE '
BEGIN {
    nrelocs = 0;
    status = 0;
    address_prefix = "";
    fixup_types["R_X86_64_64"] = 1;
    fixup_types["R_AARCH64_ABS64"] = 1;
}
# In GNU awk, this is just: return strtonum("0x" string)
# But for least-common-denominator awk, you really have to do it by hand.
function hex2num(string) {
    hexdigits["0"] = 0;
    hexdigits["1"] = 1;
    hexdigits["2"] = 2;
    hexdigits["3"] = 3;
    hexdigits["4"] = 4;
    hexdigits["5"] = 5;
    hexdigits["6"] = 6;
    hexdigits["7"] = 7;
    hexdigits["8"] = 8;
    hexdigits["9"] = 9;
    hexdigits["a"] = 10;
    hexdigits["b"] = 11;
    hexdigits["c"] = 12;
    hexdigits["d"] = 13;
    hexdigits["e"] = 14;
    hexdigits["f"] = 15;
    hexval = 0;
    while (string != "") {
      hexval = (hexval * 16) + hexdigits[substr(string, 1, 1)];
      string = substr(string, 2, length(string) - 1);
    }
    return hexval;
}
$1 == "Relocation" && $2 == "section" {
    secname = $3;
    sub(/'\''$/, "", secname);
    sub(/^'\''\.rela/, "", secname);
    next
}
NF == 0 || $1 == "Offset" { next }
!secname { exit(1); }
# Ignore standard non-allocated sections.
secname ~ /^\.debug/ || secname == ".comment" || secname == ".code-patches" { next }
# .text.boot contains code that runs before fixups.
secname == ".text.boot" { next }
$3 == "R_X86_64_PC32" || $3 == "R_X86_64_PLT32" || \
$3 == "R_X86_64_PC64" || \
$3 == "R_AARCH64_PREL32" || $3 == "R_AARCH64_PREL64" || \
$3 == "R_AARCH64_CALL26" || $3 == "R_AARCH64_JUMP26" || \
$3 == "R_AARCH64_CONDBR19" || $3 == "R_AARCH64_TSTBR14" || \
$3 == "R_AARCH64_PLT32" || \
$3 ~ /^R_AARCH64_ADR_/ || $3 ~ /^R_AARCH64_.*ABS_L/ {
    # PC-relative relocs need no fixup.
    next
}
{
    # awk handles large integers poorly, so factor out the high 40 bits.
    this_prefix = substr($1, 1, 10)
    raw_offset = substr($1, 10)
    if (address_prefix == "") {
        address_prefix = this_prefix;
    } else if (this_prefix != address_prefix) {
        print "offset", $1, "prefix", this_prefix, "!=", address_prefix > "/dev/stderr";
        status = 1;
        next;
    }
    r_offset = hex2num(raw_offset);
    type = $3;
    if (!(type in fixup_types)) {
        bad = "reloc type " type
        # As a special case, ignore known GOT cases the compiler generates in
        # the sancov.module_ctor_trace_pc_guard function.
        if ($0 ~ /__(start|stop)___sancov_guards/) {
           if (type == "R_X86_64_GOTPCRELX" || type == "R_X86_64_REX_GOTPCRELX") {
               # On x86 it got relaxed away so there is no fixup to do here.
               next
           }
        }
    } else if (secname == ".text.bootstrap16") {
        # This section is a special case with some movabs instructions
        # that can be fixed up safely but their immediates are not aligned.
        bad = "";
    } else if (r_offset % 8 != 0) {
        bad = "misaligned r_offset";
    } else if (secname !~ /^\.(ro)?data|^\.kcounter.desc|\.init_array|\.fini_array|__llvm_prf_data|asan_globals|__trace_string_refs_table/) {
        bad = "fixup in unexpected section"
    } else {
        bad = "";
    }
    if (bad == "") {
        relocs[++nrelocs] = r_offset;
	# Ancient versions of mawk (1.3.3) cannot handle too many references to the same string.
	# Instead of reusing secname, we create a new string: "secname @ offset"
	# This is only used for error reporting.
	reloc_secname[r_offset] = sprintf("%s @ %#x", secname, r_offset);
    } else {
        print "cannot handle", bad, "at", $1, "in", secname > "/dev/stderr";
        status = 1;
        objdump_cmd = sprintf("\
\"%s\" -rdlC --start-address=0x%s%.*x --stop-address=0x%s%.*x %s", objdump,
                      this_prefix, 16 - length(this_prefix), r_offset - 8,
                      this_prefix, 16 - length(this_prefix), r_offset + 8,
                      kernel);
        print "\tTry:", objdump_cmd > "/dev/stderr";
    }
}
END {
    '"$sort_relocs"'
    if (pure) {
        if (nrelocs > 0) {
            print "Binary not purely position-independent: needs", nrelocs, "fixups" > "/dev/stderr";
            for (i = 1; i <= nrelocs; ++i) {
                printf "    0x%s%.*x\n", address_prefix, 16 - length(address_prefix), reloc[i] > "/dev/stderr";
            }
            exit(1);
        }
    } else if (nrelocs == 0) {
        print "Kernel should have some fixups!" > "/dev/stderr";
        exit(1);
    }

    # 256 bytes is the max reach of a load/store post indexed instruction on arm64
    max_stride = 256;

    run_start = -1;
    run_length = 0;
    run_stride = 0;
    for (i = 1; i <= nrelocs; ++i) {
        offset = relocs[i];
        if (offset == run_start + (run_length * run_stride)) {
            ++run_length;
        } else if (offset > run_start && i > 0 && run_length == 1 &&
                   (offset - run_start) % 8 == 0 &&
                   (offset - run_start) < max_stride) {
            run_stride = offset - run_start;
            run_length = 2;
        } else {
            if (run_length > 0) {
                printf "fixup 0x%s%.*x, %u, %u // %s\n", address_prefix, 16 - length(address_prefix), run_start, run_length, run_stride, reloc_secname[run_start];
            }
            run_start = offset;
            run_length = 1;
            run_stride = 8;
        }
    }
    printf "fixup 0x%s%.*x, %u, %u // %s\n", address_prefix, 16 - length(address_prefix), run_start, run_length, run_stride, reloc_secname[run_start];
    exit(status);
}'
}

set -e
if [ -n "$BASH_VERSION" ]; then
  set -o pipefail
fi

trap 'rm -f "$OUTFILE" ${DEPFILE:+"$DEPFILE"}' ERR INT HUP TERM

if [[ "$KERNEL" == @* ]]; then
  KERNEL="$(< "${KERNEL#@}")"
fi

LC_ALL=C "$READELF" -W -r "$KERNEL" | grok_fixups > "$OUTFILE"

if [ -n "$DEPFILE" ]; then
  echo "$OUTFILE: $KERNEL" > "$DEPFILE"
fi
