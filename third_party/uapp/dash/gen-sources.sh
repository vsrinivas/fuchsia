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
#  * src/signames.c (if you uncomment the lines at the end)
#  * src/syntax.h
#  * src/syntax.c
#  * src/token.h
#  * src/token_vars.h

cd src

set -ex

gcc -E -x c -o builtins.def builtins.def.in -DJOBS=0

sh mktokens
sh mkbuiltins builtins.def

gcc mkinit.c -o mkinit
gcc mknodes.c -o mknodes
gcc mksyntax.c -o mksyntax

DASH_CFILES="alias.c arith_yacc.c arith_yylex.c cd.c error.c eval.c exec.c expand.c \
  input.c jobs.c main.c memalloc.c miscbltin.c \
  mystring.c options.c parser.c redir.c show.c trap.c output.c \
  bltin/printf.c system.c bltin/test.c bltin/times.c var.c"

./mkinit $DASH_CFILES
./mknodes nodetypes nodes.c.pat
./mksyntax

rm mkinit
rm mknodes
rm mksyntax

# Uncomment the following lines to generate signames.c:
#
#   gcc mksignames.c -o mksignames
#   ./mksignames
#   rm mksignames
