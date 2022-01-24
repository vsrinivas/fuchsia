#!/usr/bin/env bash
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#### CATEGORY=Run, inspect and debug
#### EXECUTABLE=${PREBUILT_3P_DIR}/python3/${HOST_PLATFORM}/bin/python3 ${FUCHSIA_DIR}/tools/devshell/contrib/sniff.py
### Fuchsia packet capture and display tool
## fx sniff captures the packets flowing in-and-out the Fuchsia target device
## and displays the packets in a useful view. This is to run on the development
## host.
##
## fx sniff will automatically filter out fx-workflow related packets such as ssh,
## package server, logs, zxdb, etc., so that the user can focus on the application
## of interest. For those who need to debug the fx workflow itself,
## the full command under use is also printed; one can easily modify to meet
## their own needs.
##
## [Typical usages]
## $ fx sniff wlan                    # capture packets over WLAN interface,
##                                    # and show their summaries
## $ fx sniff --wireshark wlan        # capture packets over WLAN interface,
##                                    # and start wireshark GUI for display
## $ fx sniff --file myfile wlan      # capture packets and store
##                                    # at //out/myfile.pcapng
## $ fx sniff -t 10 wlan              # capture for 10 sec
## $ fx sniff --help                  # show all command line options
