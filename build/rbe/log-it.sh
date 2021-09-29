#!/bin/sh
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

usage() {
  cat <<EOF
$0 --log LOGFILE -- command...

This script redirects stdout and stderr to a log file *without*
using special shell lexical redirect tokens like '>'.
This does the exact same thing as: 'command > LOGFILE 2>&1'.

This is useful in situations where the stderr output is too large
to transmit over an RPC.
EOF
}

logfile=

for opt
do
  # handle --option arg
  if test -n "$prev_opt"
  then
    eval "$prev_opt"=\$opt
    prev_opt=
    shift
    continue
  fi
  # Extract optarg from --opt=optarg
  case "$opt" in
    *=?*) optarg=$(expr "X$opt" : '[^=]*=\(.*\)') ;;
    *=) optarg= ;;
  esac
  case "$opt" in
    --help|-h) usage ; exit ;;
    --log=*) logfile="$optarg" ;;
    --log) prev_opt=logfile ;;
    # stop option processing
    --) shift; break ;;
    *) echo "Unknown option: $opt"; usage; exit 1 ;;
  esac
  shift
done

test -n "$logfile" || { echo "Missing required --log argument." ; exit 1 ;}

# /usr/bin/env, in case command starts with an environment variable.
exec /usr/bin/env "$@" > "$logfile" 2>&1

