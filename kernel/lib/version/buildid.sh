#!/bin/bash

# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

eval `date -u +'BYR=%Y BMON=%-m BDOM=%-d BHR=%-H BMIN=%-M'`
chr () {
    printf \\$(($1/64*100+$1%64/8*10+$1%8))
}
b36 () {
    if [ $1 -le 9 ]; then echo $1; else chr $((0x41 + $1 - 10)); fi
}
id=$(printf '%c%c%c%c%c\n' `chr $((0x41 + $BYR - 2011))` `b36 $BMON` `b36 $BDOM` `b36 $BHR` `b36 $(($BMIN/2))`)

if [[ $# -eq 1 ]]; then
  cat > "$1" <<END
#ifndef __BUILDID_H
#define __BUILDID_H
#define ${id}
#endif
END
fi
