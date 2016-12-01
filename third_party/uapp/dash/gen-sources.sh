#!/bin/sh
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script generates the following files:
#
#  * src/builtins.h
#  * src/builtins.c
#  * src/builtins.def
#  * src/init.c
#  * src/nodes.h
#  * src/nodes.c
#  * src/signames.c
#  * src/syntax.h
#  * src/syntax.c
#  * src/token.h
#  * src/token_vars.h
#
# This script requires mksignames.c, which is not part of this repository.
# Consider using the mksignames.c from upstream dash.

cd src

if [ ! -f "mksignames.c" ]
then
  echo "Missing mksignames.c."
  exit 1
fi

set -ex

gcc -E -x c -o builtins.def builtins.def.in -DJOBS=0

sh mktokens
sh mkbuiltins builtins.def

gcc mkinit.c -o mkinit
gcc mknodes.c -o mknodes
gcc mksyntax.c -o mksyntax
gcc mksignames.c -o mksignames

DASH_CFILES="alias.c arith_yacc.c arith_yylex.c cd.c error.c eval.c exec.c expand.c \
  histedit.c input.c jobs.c mail.c main.c memalloc.c miscbltin.c \
  mystring.c options.c parser.c redir.c show.c trap.c output.c \
  bltin/printf.c system.c bltin/test.c bltin/times.c var.c"

./mkinit $DASH_CFILES
./mknodes nodetypes nodes.c.pat
./mksyntax
./mksignames

rm mkinit
rm mknodes
rm mksyntax
rm mksignames
