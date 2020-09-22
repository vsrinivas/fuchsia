# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#### CATEGORY=Code submission and review
#### EXECUTABLE=${FUCHSIA_DIR}/tools/devshell/contrib/gerrit-submit-lib/submit.py
### Submits chains of CLs to Gerrit
## usage: submit.py [-h] [--host HOST] [--num-retries N] CL
##
## Submit a chain of CLs, specified by giving the CL number of the end of the
## chain. The command will poll indefinitely until the chain is submitted or an
## error is detected.
##
## The tool can be safely cancelled at any time. When restarted, it will resume
## where it left off.
##
## For example, given a chain of three CLs:
##
##   101: Start hacking on 'foo'.
##   102: More hacking on 'foo'.
##   103: Finish hacking on 'foo'.
##
## The command:
##
##   fx gerrit-submit 103
##
## will:
##
##   1. Add a CQ+1 vote to all the CLs, to start testing them.
##   2. Add a CQ+2 vote for the first CL, and wait for it to be submitted.
##   3. Add a CQ+2 vote for the second CL, and wait for it to be submitted.
##   4. Add a CQ+2 vote for the last CL, and wait for it to be submitted.
##
## Adding a CQ+1 to every CL at the beginning speeds up submission: CQ won't
## need to re-test intermediate CLs if they are not modified in the meantime.
##
## If any CL is not ready to submit (for example, it is missing a vote, or has
## unresolved comments), the tool will abort early.
##
## By default, the tool will use the "fuchsia-review.googlesource.com" Gerrit
## instances. Other instances can be specified using the "--host" parameter:
##
##    fx gerrit-submit --host myteam-review.googlesource.com 12345
##
##
## positional arguments:
##   CL               Gerrit CL to submit. May either be a CL number or Gerrit Change-ID.
##
## optional arguments:
##   -h, --help       show this help message and exit
##   --host HOST      Gerrit host to connect to.
##                    Defaults to "fuchsia-review.googlesource.com".
##   --num-retries N  number of times to retry a failed submission.
##                    Defaults to 0.
