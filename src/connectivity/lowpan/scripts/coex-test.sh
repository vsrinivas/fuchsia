#!/bin/bash
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

COEX_TEST_VERSION=1.5

# USAGE INSTRUCTIONS
# ==================
#
# The parameters for running this coex test are passed via environment
# variables. See the included source code for a full list, but at a minimum
# you must let the script know how to communicate with DUT and FDEV.
#
# When used in-tree with devices that are running builds from this machine,
# you can run the script like this:
#
# ```
# $ DUT=fuchsia-ac67-8480-745c \
#   FDEV=fuchsia-00e0-4c05-1c19 \
#   src/connectivity/lowpan/scripts/coex-test.sh
# ```
#
# If you have some other setup that prevents you from using `fx`, but you still
# have a way to execute shell commands on the devices (like via `ssh`), you can
# specify how to execute the shell commands like so:
#
# ```
# $ FSHELL_DUT="ssh fuchsia-ac67-8480-745c.local." \
#   FSHELL_FDEV="ssh fuchsia-00e0-4c05-1c19.local." \
#   src/connectivity/lowpan/scripts/coex-test.sh
# ```
#
# Additional parameters can be overridden, such as test duration or the
# IP addresses of the individual devices:
#
# ```
# $ FSHELL_DUT="ssh fuchsia-ac67-8480-745c.local." \
#   FSHELL_FDEV="ssh fuchsia-00e0-4c05-1c19.local." \
#   DUT_IPv4="192.168.1.2" \
#   TEST_DURATION="120" \
#   src/connectivity/lowpan/scripts/coex-test.sh
# ```
#
# By default, the test output files are placed in `coex-test` in the current
# directory.

set -e
#set -x

TEST_DURATION=${TEST_DURATION-60}

DUT=${DUT-fuchsia-ac67-8480-745c}
FDEV=${FDEV-fuchsia-00e0-4c05-1c19}

DATASET=${DATASET-0e080000000000010000000300000d35060004001fffc0020800010203040506070708fd29a8a7d4944dd6051087e832dc50e9de087d1ff4f6133356a7030c54657374204e6574776f726b0102d5410410c3f59368445a1b6106be420a706d4cc90c0402a0f7f8}

FSHELL_DUT=${FSHELL_DUT-fx -d $DUT shell}
FSHELL_FDEV=${FSHELL_FDEV-fx -d $FDEV shell}

OT_CLI_DUT=${OT_CLI_DUT-${FSHELL_DUT} lowpanctl mfg}
OT_CLI_FDEV=${OT_CLI_FDEV-${FSHELL_FDEV} lowpanctl mfg}

CURR_DIR=$(pwd)

OUTPUT_DIR=${OUTPUT_DIR-$CURR_DIR/coex-output}
mkdir -p "${OUTPUT_DIR}"
OUTPUT_DIR=$(cd "${OUTPUT_DIR}" && pwd)

FLOOD_DURATION=$((TEST_DURATION+20))
PING_DURATION=$((TEST_DURATION))

IPERF3_THREAD_OPTIONS=${IPERF3_THREAD_OPTIONS--u -b 40k}
IPERF3_WIFI_OPTIONS=${IPERF3_WIFI_OPTIONS-}

# If $FUCHSIA_DIR isn't set, take a guess.
FUCHSIA_DIR=${FUCHSIA_DIR-$(dirname "$0")/../../../../..}

if [ -t 1 ]
then
    export ESC_BOLD=${ESC_BOLD-`printf '\033[0;1m'`} # '
    export ESC_NORMAL=${ESC_NORMAL-`printf '\033[0m'`} # '
fi

# Make sure we have a way to run `fx`, if it is available.
test -d "${FUCHSIA_DIR}" && cd "${FUCHSIA_DIR}"

die() {
  echo " *** ${ESC_BOLD}ERROR${ESC_NORMAL}: $*" > /dev/stderr
  exit 1
}

announce() {
  echo "   "
  echo " * ${ESC_BOLD}$*${ESC_NORMAL}"
}

announce_test_started() {
  echo "   "
  echo " * ${ESC_BOLD}Starting test $*${ESC_NORMAL}"
}

measure_parameters() {
  local name
  name=${1-UNKN}
  ping -c "${PING_DURATION}" "${DUT_IPv4}" 2>&1 | tee "${OUTPUT_DIR}/$name.ping.HOST_TO_DUT.txt" &
  ping_host_pid=$!
  ${FSHELL_DUT} lowpanctl mfg ping "${FDEV_IPv6}" 56 "$((PING_DURATION*10/6))" 0.625 1 2>&1 | tee "${OUTPUT_DIR}/$name.ping.DUT_TO_FDEV.txt"

  echo "Waiting for ping to finish..."
  wait ${ping_host_pid}

  (
    ${FSHELL_DUT} lowpanctl get-counters 2>&1
    ${FSHELL_DUT} lowpanctl mfg counters mle 2>&1
  ) | tee "${OUTPUT_DIR}/$name.counters.DUT.txt"
  (
    ${FSHELL_FDEV} lowpanctl get-counters 2>&1
    ${FSHELL_FDEV} lowpanctl mfg counters mle 2>&1
  ) | tee "${OUTPUT_DIR}/$name.counters.FDEV.txt"
  (
    echo "COEX_TEST_VERSION=${COEX_TEST_VERSION}"
    echo "THREAD_CCA_THRESHOLD=\"$(${OT_CLI_DUT} ccathreshold | head -n 1)\""
    if [ "$DUT_HAS_WL" = "1" ]
    then
        echo "WL_VER=\"$(${FSHELL_DUT} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx ver | xargs)\""
        echo "WL_PM=$(${FSHELL_DUT} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx PM) # Power management mode"
        echo "WL_ZBC_PARAMS_0=$(${FSHELL_DUT} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 0) # Coex Enabled/Disabled"
        echo "WL_ZBC_PARAMS_1=$(${FSHELL_DUT} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 1)"
        echo "WL_ZBC_PARAMS_4=$(${FSHELL_DUT} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 4)"
        echo "WL_ZBC_PARAMS_5=$(${FSHELL_DUT} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 5) # WLAN abort counter"
        echo "WL_ZBC_PARAMS_6=$(${FSHELL_DUT} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 6) # RX response abort counter"
        echo "WL_ZBC_PARAMS_7=$(${FSHELL_DUT} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 7)"
        echo "WL_ZBC_PARAMS_8=$(${FSHELL_DUT} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 8) # Low priority coex table"
        echo "WL_ZBC_PARAMS_12=$(${FSHELL_DUT} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 12) # High priority coex table"
        echo "WL_ZBC_PARAMS_22=$(${FSHELL_DUT} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 22)"
        echo "WL_ZBC_PARAMS_26=$(${FSHELL_DUT} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 26) # 802.15.4 request counter"
    fi
  ) > "${OUTPUT_DIR}/$name.other.DUT.txt"
  (
    echo "COEX_TEST_VERSION=${COEX_TEST_VERSION}"
    echo "THREAD_CCA_THRESHOLD=\"$(${OT_CLI_FDEV} ccathreshold | head -n 1)\""
    if [ "$FDEV_HAS_WL" = "1" ]
    then
        echo "WL_VER=\"$(${FSHELL_FDEV} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx ver | xargs)\""
        echo "WL_PM=$(${FSHELL_FDEV} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx PM) # Power management mode"
        echo "WL_ZBC_PARAMS_0=$(${FSHELL_FDEV} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 0) # Coex Enabled/Disabled"
        echo "WL_ZBC_PARAMS_1=$(${FSHELL_FDEV} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 1)"
        echo "WL_ZBC_PARAMS_4=$(${FSHELL_FDEV} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 4)"
        echo "WL_ZBC_PARAMS_5=$(${FSHELL_FDEV} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 5) # WLAN abort counter"
        echo "WL_ZBC_PARAMS_6=$(${FSHELL_FDEV} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 6) # RX response abort counter"
        echo "WL_ZBC_PARAMS_7=$(${FSHELL_FDEV} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 7)"
        echo "WL_ZBC_PARAMS_8=$(${FSHELL_FDEV} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 8) # Low priority coex table"
        echo "WL_ZBC_PARAMS_12=$(${FSHELL_FDEV} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 12) # High priority coex table"
        echo "WL_ZBC_PARAMS_22=$(${FSHELL_FDEV} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 22)"
        echo "WL_ZBC_PARAMS_26=$(${FSHELL_FDEV} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx zbc_params 26) # 802.15.4 request counter"
    fi
  ) > "${OUTPUT_DIR}/$name.other.FDEV.txt"
}

start_DUT_iperf_server() {
  echo "Starting iperf3 server on DUT"
  ${FSHELL_DUT} killall iperf3 >/dev/null 2>&1 || true
  ${FSHELL_DUT} iperf3 -s -i 1 > "${OUTPUT_DIR}/DUT.iperf-server.txt" 2>&1 &
  DUT_IPERF_SERVER_PID=$!
}

start_FDEV_iperf_server() {
  echo "Starting iperf3 server on FDEV"
  ${FSHELL_FDEV} killall iperf3 >/dev/null 2>&1 || true
  ${FSHELL_FDEV} iperf3 -s -i 1 > "${OUTPUT_DIR}/FDEV.iperf-server.txt" 2>&1 &
  FDEV_IPERF_SERVER_PID=$!
}

setup_DUT() {
  local dut_dataset

  echo "Determining DUT reachability..."
  ${FSHELL_DUT} echo "DUT is reachable." || die "DUT is not reachable."

  echo "Determining if DUT has LoWPAN running..."
  ${FSHELL_DUT} lowpanctl status > /dev/null 2>&1 || die "LoWPAN is not running on DUT."
  echo "LoWPAN device is running on DUT."

  echo "Checking that DUT has iperf3..."
  ${FSHELL_DUT} which iperf3 > /dev/null 2>&1 || die "DUT does not appear to have iperf3"

  ${FSHELL_DUT} log_listener --since_now --severity DEBUG > "$OUTPUT_DIR/DUT.log_listener.txt" &
  DUT_LOG_LISTENER_PID=$!

  dut_dataset=$(${FSHELL_DUT} lowpanctl dataset get --format raw)
  if test "$dut_dataset" '!=' "$DATASET"
  then
    echo "Setting up DUT..."
    ${FSHELL_DUT} lowpanctl leave || die "Unable to leave LoWPAN on DUT."
    ${FSHELL_DUT} lowpanctl dataset set --tlvs "$DATASET"
  else
    echo "DUT is already set up."
  fi
}

setup_FDEV() {
  local fdev_dataset

  echo "Determining FDEV reachability..."
  ${FSHELL_FDEV} echo "FDEV is reachable." || die "FDEV is not reachable."

  echo "Determining if FDEV has LoWPAN running..."
  ${FSHELL_FDEV} lowpanctl status > /dev/null 2>&1 || die "LoWPAN is not running on FDEV."
  echo "LoWPAN device is running on FDEV."

  echo "Checking that FDEV has iperf3..."
  ${FSHELL_FDEV} which iperf3 > /dev/null 2>&1 || die "FDEV does not appear to have iperf3"

  ${FSHELL_FDEV} log_listener --since_now --severity DEBUG > "$OUTPUT_DIR/FDEV.log_listener.txt" &
  FDEV_LOG_LISTENER_PID=$!

  fdev_dataset=$(${FSHELL_FDEV} lowpanctl dataset get --format raw)
  if test "$fdev_dataset" '!=' "$DATASET"
  then
    echo "Setting up FDEV..."
    ${FSHELL_FDEV} lowpanctl leave || die "Unable to leave LoWPAN on FDEV."
    ${FSHELL_FDEV} lowpanctl dataset set --tlvs "$DATASET"
  else
    echo "FDEV is already set up."
    echo "Checking mode..."
    fdev_mode=$(${FSHELL_FDEV} lowpanctl mfg mode | sed 's/\r//g' | head -n 1 )
    if test "$fdev_mode" '!=' "rdn"
    then
      echo "Mode is not 'rdn' (it was '$fdev_mode'), changing mode"
      ${FSHELL_FDEV} lowpanctl set-active false > /dev/null 2>&1 || die "Unable to deactivate LoWPAN on FDEV."
      ${FSHELL_FDEV} lowpanctl mfg mode rdn > /dev/null 2>&1 || die "Unable to change mode for LoWPAN on FDEV."
    else
      echo "Mode is 'rdn'. Good."
    fi
    ${FSHELL_FDEV} lowpanctl set-active true > /dev/null 2>&1 || die "Unable to activate LoWPAN on FDEV."
  fi
}

check_for_problems() {
  if grep -i "energy scan" "$OUTPUT_DIR/DUT.log_listener.txt" > /dev/null 2>&1
  then echo "${ESC_BOLD}WARNING${ESC_NORMAL}: Energy scan was detected in DUT logs, results may be misleading."
  fi

  if grep -i "error" "${OUTPUT_DIR}/DUT.iperf-server.txt" > /dev/null 2>&1
  then echo "${ESC_BOLD}WARNING${ESC_NORMAL}: DUT iperf3 server had an error, results may be incomplete."
  fi

  if grep -i "energy scan" "$OUTPUT_DIR/FDEV.log_listener.txt" > /dev/null 2>&1
  then echo "${ESC_BOLD}WARNING${ESC_NORMAL}: Energy scan was detected in FDEV logs, results may be misleading."
  fi

  if grep -i "error" "${OUTPUT_DIR}/FDEV.iperf-server.txt" > /dev/null 2>&1
  then echo "${ESC_BOLD}WARNING${ESC_NORMAL}: FDEV iperf3 server had an error, results may be incomplete."
  fi
}

cleanup() {
  echo " *** Finished at $(date)"
  jobs=$(jobs -p)
  (
    pkill -P ${DUT_IPERF_SERVER_PID} || true
    pkill -P ${FDEV_IPERF_SERVER_PID} || true
    pkill -P ${FDEV_LOG_LISTENER_PID} || true
    pkill -P ${DUT_LOG_LISTENER_PID} || true
    kill $jobs || true
  ) >/dev/null 2>&1
  sleep 1
  (
    kill -9 ${DUT_IPERF_SERVER_PID} ${FDEV_IPERF_SERVER_PID} ${FDEV_LOG_LISTENER_PID} ${DUT_LOG_LISTENER_PID} || true
    kill -9 $jobs || true
  ) >/dev/null 2>&1

  check_for_problems
}

get_host_ipv4() {
  ifconfig | grep "inet " | sed 's/.* \([0-9]\{1,3\}\.[0-9]\{1,3\}\.[0-9]\{1,3\}\.[0-9]\{1,3\}\) .*/\1/' | grep -v '^127\.' | head -n 1
}

get_dut_ipv4() {
  ${FSHELL_DUT} net if list wlan | grep "addr " | sed 's/.* \([0-9]\{1,3\}\.[0-9]\{1,3\}\.[0-9]\{1,3\}\.[0-9]\{1,3\}\)\/.*/\1/' | grep -v addr | head -n 1
}

get_dut_ipv6() {
  ${FSHELL_DUT} net if list lowpan | grep "addr " | sed 's/.* \([0-9a-fA-F:]*\)\/64/\1/' | grep -v addr | sort | head -n 1
}

get_fdev_ipv6() {
  ${FSHELL_FDEV} net if list lowpan | grep "addr " | sed 's/.* \([0-9a-fA-F:]*\)\/64/\1/' | grep -v addr | sort | head -n 1
}

check_devices() {
  ${FSHELL_DUT} lowpanctl status >/dev/null 2>&1 || die "LoWPAN on DUT is no longer running."
  ${FSHELL_FDEV} lowpanctl status >/dev/null 2>&1 || die "LoWPAN on FDEV is no longer running."
}

do_ts1() {
  announce_test_started TS1
  measure_parameters TS1
  check_devices
}

do_ts2() {
  announce_test_started TS2

  iperf3 -c "${DUT_IPv4}" -t ${FLOOD_DURATION} -i 1 ${IPERF3_WIFI_OPTIONS} | tee "$OUTPUT_DIR/TS2.iperf3.HOST_TO_DUT.txt" &
  iperf3_host_pid=$!
  measure_parameters TS2

  echo "Waiting for iperf3 to finish..."
  wait ${iperf3_host_pid}

  check_devices
}

do_ts3() {
  announce_test_started TS3

  ${FSHELL_DUT} iperf3 -c "${FDEV_IPv6}" -t ${FLOOD_DURATION} -i 1 ${IPERF3_THREAD_OPTIONS} | tee "$OUTPUT_DIR/TS3.iperf3.DUT_TO_FDEV.txt" &
  iperf3_dut_pid=$!
  measure_parameters TS3

  echo "Waiting for iperf3 to finish..."
  wait ${iperf3_dut_pid}

  check_devices
}

do_ts4() {
  announce_test_started TS4

  iperf3 -c "${DUT_IPv4}" -t ${FLOOD_DURATION} -i 1 ${IPERF3_WIFI_OPTIONS} | tee "$OUTPUT_DIR/TS4.iperf3.HOST_TO_DUT.txt" &
  iperf3_host_pid=$!

  ${FSHELL_DUT} iperf3 -c "${FDEV_IPv6}" -t ${FLOOD_DURATION} -i 1 ${IPERF3_THREAD_OPTIONS} | tee "$OUTPUT_DIR/TS4.iperf3.DUT_TO_FDEV.txt" &
  iperf3_dut_pid=$!

  measure_parameters TS4

  echo "Waiting for iperf3 to finish..."
  wait ${iperf3_dut_pid} ${iperf3_host_pid}

  check_devices
}

do_ts5() {
  announce_test_started TS5

  iperf3 -c "${DUT_IPv4}" -t ${FLOOD_DURATION} -i 1 -R ${IPERF3_WIFI_OPTIONS} | tee "$OUTPUT_DIR/TS5.iperf3.DUT_TO_HOST.txt" &
  iperf3_host_pid=$!

  measure_parameters TS5

  echo "Waiting for iperf3 to finish..."
  wait ${iperf3_host_pid}

  check_devices
}

do_ts6() {
  announce_test_started TS6

  iperf3 -c "${DUT_IPv4}" -t ${FLOOD_DURATION} -i 1 -R ${IPERF3_WIFI_OPTIONS} | tee "$OUTPUT_DIR/TS6.iperf3.DUT_TO_HOST.txt" &
  iperf3_host_pid=$!

  ${FSHELL_DUT} iperf3 -c "${FDEV_IPv6}" -t ${FLOOD_DURATION} -i 1 ${IPERF3_THREAD_OPTIONS} | tee "$OUTPUT_DIR/TS6.iperf3.DUT_TO_FDEV.txt" &
  iperf3_dut_pid=$!

  measure_parameters TS6

  echo "Waiting for iperf3 to finish..."
  wait ${iperf3_dut_pid} ${iperf3_host_pid}

  check_devices
}

KPI12_IPERF3_THREAD_OPTIONS=${KPI12_IPERF3_THREAD_OPTIONS--u -b 40k}
KPI12_IPERF3_WIFI_OPTIONS=${KPI12_IPERF3_WIFI_OPTIONS--u -b 10m}

do_kpi1() {
  announce_test_started KPI1

  iperf3 -c "${DUT_IPv4}" -t ${FLOOD_DURATION} -i 1 ${KPI12_IPERF3_WIFI_OPTIONS} | tee "$OUTPUT_DIR/KPI1.iperf3.HOST_TO_DUT.txt" &
  iperf3_host_pid=$!

  ${FSHELL_DUT} iperf3 -c "${FDEV_IPv6}" -t ${FLOOD_DURATION} -i 1 ${KPI12_IPERF3_THREAD_OPTIONS} | tee "$OUTPUT_DIR/KPI1.iperf3.DUT_TO_FDEV.txt" &
  iperf3_dut_pid=$!

  measure_parameters KPI1

  echo "Waiting for iperf3 to finish..."
  wait ${iperf3_dut_pid} ${iperf3_host_pid}

  check_devices
}

do_kpi2() {
  announce_test_started KPI2

  iperf3 -c "${DUT_IPv4}" -t ${FLOOD_DURATION} -i 1 -R ${KPI12_IPERF3_WIFI_OPTIONS} | tee "$OUTPUT_DIR/KPI2.iperf3.DUT_TO_HOST.txt" &
  iperf3_host_pid=$!

  ${FSHELL_DUT} iperf3 -c "${FDEV_IPv6}" -t ${FLOOD_DURATION} -i 1 ${KPI12_IPERF3_THREAD_OPTIONS} | tee "$OUTPUT_DIR/KPI2.iperf3.DUT_TO_FDEV.txt" &
  iperf3_dut_pid=$!

  measure_parameters KPI2

  echo "Waiting for iperf3 to finish..."
  wait ${iperf3_dut_pid} ${iperf3_host_pid}

  check_devices
}

KPI3_IPERF3_WIFI_OPTIONS=${KPI3_IPERF3_WIFI_OPTIONS--u -b 50m}

do_kpi3() {
  announce_test_started KPI3

  # Make FDEV a an SED
  ${FSHELL_FDEV} lowpanctl mfg thread stop > /dev/null 2>&1 || die "Unable to deactivate LoWPAN on FDEV."
  ${FSHELL_FDEV} lowpanctl mfg mode - > /dev/null 2>&1 || die "Unable to change mode for LoWPAN on FDEV."
  ${FSHELL_FDEV} lowpanctl mfg thread start > /dev/null 2>&1 || die "Unable to deactivate LoWPAN on FDEV."
  ${FSHELL_FDEV} lowpanctl mfg pollperiod 39 > /dev/null 2>&1 || die "Unable to change poll period on FDEV."

  iperf3 -c "${DUT_IPv4}" -t ${FLOOD_DURATION} -i 1 -R ${KPI3_IPERF3_WIFI_OPTIONS} | tee "$OUTPUT_DIR/KPI3.iperf3.DUT_TO_HOST.txt" &
  iperf3_host_pid=$!

  measure_parameters KPI3

  echo "Waiting for iperf3 to finish..."
  wait ${iperf3_host_pid}

  check_devices

  # Turn FDEV back into a router
  ${FSHELL_FDEV} lowpanctl mfg thread stop > /dev/null 2>&1 || die "Unable to deactivate LoWPAN on FDEV."
  ${FSHELL_FDEV} lowpanctl mfg mode rdn > /dev/null 2>&1 || die "Unable to change mode for LoWPAN on FDEV."
  ${FSHELL_FDEV} lowpanctl mfg thread start > /dev/null 2>&1 || die "Unable to deactivate LoWPAN on FDEV."
}

KPI4_IPERF3_WIFI_OPTIONS=${KPI3_IPERF3_WIFI_OPTIONS--u -b 40m}

do_kpi4() {
  announce_test_started KPI4

  # Make FDEV a an SED
  ${FSHELL_FDEV} lowpanctl mfg thread stop > /dev/null 2>&1 || die "Unable to deactivate LoWPAN on FDEV."
  ${FSHELL_FDEV} lowpanctl mfg mode - > /dev/null 2>&1 || die "Unable to change mode for LoWPAN on FDEV."
  ${FSHELL_FDEV} lowpanctl mfg thread start > /dev/null 2>&1 || die "Unable to deactivate LoWPAN on FDEV."
  ${FSHELL_FDEV} lowpanctl mfg pollperiod 39 > /dev/null 2>&1 || die "Unable to change poll period on FDEV."

  iperf3 -c "${DUT_IPv4}" -t ${FLOOD_DURATION} -i 1 ${KPI4_IPERF3_WIFI_OPTIONS} | tee "$OUTPUT_DIR/KPI4.iperf3.HOST_TO_DUT.txt" &
  iperf3_host_pid=$!

  measure_parameters KPI4

  echo "Waiting for iperf3 to finish..."
  wait ${iperf3_host_pid}

  check_devices

  # Turn FDEV back into a router
  ${FSHELL_FDEV} lowpanctl mfg thread stop > /dev/null 2>&1 || die "Unable to deactivate LoWPAN on FDEV."
  ${FSHELL_FDEV} lowpanctl mfg mode rdn > /dev/null 2>&1 || die "Unable to change mode for LoWPAN on FDEV."
  ${FSHELL_FDEV} lowpanctl mfg thread start > /dev/null 2>&1 || die "Unable to deactivate LoWPAN on FDEV."
}


################################################################################

echo "Fuchsia Thread/WiFi Coex Test, v${COEX_TEST_VERSION}"
echo "-----------------------------------"
echo "Started at $(date)"

which iperf3 > /dev/null 2>&1 || die "iperf3 was not found in path"
which ping6 > /dev/null 2>&1 || die "ping6 was not found in path"

setup_DUT
setup_FDEV

if (${FSHELL_DUT} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx ver > /dev/null 2>&1)
then
  echo 'DUT has `wl` command.'
  DUT_HAS_WL=1
  echo "Turning off power save on DUT..."
  ${FSHELL_DUT} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx PM 0 > /dev/null 2>&1
else
  echo 'DUT DOES NOT HAVE `wl` COMMAND.'
fi

if (${FSHELL_FDEV} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx ver > /dev/null 2>&1)
then
  echo 'FDEV has `wl` command.'
  FDEV_HAS_WL=1
  echo "Turning off power save on FDEV..."
  ${FSHELL_FDEV} run fuchsia-pkg://fuchsia.com/wl#meta/wl.cmx PM 0 > /dev/null 2>&1
else
  echo 'FDEV DOES NOT HAVE `wl` COMMAND.'
fi

sleep 1

start_DUT_iperf_server
start_FDEV_iperf_server

trap cleanup EXIT

HOST_IPv4=${HOST_IPv4-$(get_host_ipv4)}
echo "HOST_IPv4=${HOST_IPv4}"

DUT_IPv4=${DUT_IPv4-$(get_dut_ipv4)}
echo "DUT_IPv4=${DUT_IPv4}"

DUT_IPv6=${DUT_IPv6-$(get_dut_ipv6)}
echo "DUT_IPv6=${DUT_IPv6}"

FDEV_IPv6=${FDEV_IPv6-$(get_fdev_ipv6)}
echo "FDEV_IPv6=${FDEV_IPv6}"

test x"$HOST_IPv4" '!=' x"" || die Unable to get HOST_IPv4
test x"$DUT_IPv4" '!=' x"" || die Unable to get DUT_IPv4
test x"$DUT_IPv6" '!=' x"" || die Unable to get DUT_IPv6
test x"$FDEV_IPv6" '!=' x"" || die Unable to get FDEV_IPv6

if test $# -ge 1
then
  while test $# -ge 1
  do
    case "$1" in
    ts1)
      do_ts1
      ;;
    ts2)
      do_ts2
      ;;
    ts3)
      do_ts3
      ;;
    ts4)
      do_ts4
      ;;
    ts5)
      do_ts5
      ;;
    ts6)
      do_ts6
      ;;
    kpi1)
      do_kpi1
      ;;
    kpi2)
      do_kpi2
      ;;
    kpi3)
      do_kpi3
      ;;
    kpi4)
      do_kpi4
      ;;
    help|--help|-h)
      echo "Run the script with the arguments specifying which tests you'd like"
      echo "to run. If you specify no arguments, all tests will be run."
      echo ""
      echo " * ts1: Baseline"
      echo " * ts2: WiFi HOST->DUT 40kbps"
      echo " * ts3: Thread DUT->FDEV 40kbps"
      echo " * ts4: Both WiFi HOST->DUT Saturation and Thread DUT->FDEV 40kbps"
      echo " * ts5: WiFi DUT->HOST 40kbps"
      echo " * ts6: Both WiFi DUT->HOST Saturation and Thread DUT->FDEV 40kbps"
      echo " * kpi1: WiFi HOST->DUT 10mbps and Thread DUT->FDEV 40kbps"
      echo " * kpi2: WiFi DUT->HOST 10mbps and Thread DUT->FDEV 40kbps"
      echo " * kpi3: WiFi DUT->HOST max w/sleepy-FDEV"
      echo " * kpi4: WiFi HOST->DUT 90% w/sleepy-FDEV"
      echo ""
      ;;
    *)
      die "Unknown test $1, options are ts1, ts2, ts3, ts4, ts5, ts6, kpi1, kpi2, kpi3, kpi4"
      ;;
    esac
    shift
  done
else
  # Run through all the tests
  echo "A specific test was not specified, running all tests..."
  do_ts1
  do_ts2
  do_ts3
  do_ts4
  do_ts5
  do_ts6
  do_kpi1
  do_kpi2
  do_kpi3
  do_kpi4
fi

announce "Coex Test Complete, see $OUTPUT_DIR for output"
