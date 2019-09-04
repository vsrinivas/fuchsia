// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
Package ninjalog provides ninja_log parser

It support ninja log v5.

 # ninja log v5
 <start>	<end>	<restat>	<target>	<cmdhash>

where
 <start> = start time since ninja starts in msec.
 <end>   = end time since ninja starts in msec.
 <restat> = restat time in epoch.
 <target> = target (output) filename
 <cmdhash> = hash of command line (?)

It assumes steps in the last build will be ascendent order of <end>.

It also supports metadata added by chromium's buildbot compile.py.
metadata is added after

 # end of ninja log

and written in json format.

*/
package ninjalog
