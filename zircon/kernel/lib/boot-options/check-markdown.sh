#!/bin/sh
# Copyright 2020 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

rm -f "$3"

if cmp -s "$1" "$2"; then
  echo OK > "$3"
  exit 0
fi

echo >&2 "*** $2 is out of date"
echo >&2 "*** Fix this by running:"
echo >&2 '\\\'
echo >&2 ""
echo >&2 "cp $1 $2"
echo >&2 ""
echo >&2 '\\\'echo >&2 '\\\'
exit 1
