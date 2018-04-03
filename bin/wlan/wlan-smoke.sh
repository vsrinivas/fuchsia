#!/boot/bin/sh
#
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Fuchsia WLAN smoke test
#
# This code is an end-to-end integration test
# This code is designed as a quick-n-dirty smoke test of WLAN functionality.
# This integration test largely involves an external system from the WLAN
# access point to the Internet, which makes testing performed in
# uncontrolled environment.

TEST_LOG="/tmp/wlan-doctor.log"

log () {
  msg="$*"
  now=$(date)
  echo "${now}  wlan smoke: ${msg}"
  echo "${now}  wlan smoke: ${msg}" >> ${TEST_LOG}
}

log_pass() {
  msg="$*"
  log "[PASS] ${msg}"
}

log_fail() {
  msg="$*"
  log "[FAIL] ${msg}"
}

ping_dst() {
  dst="$1"
  cmd="ping -c 2 ${dst}"
  ${cmd} > /dev/null 2>&1

  if [ "$?" -ne 0 ]; then
    log_fail "${cmd}"
    test_teardown
    return 1
  else
    log_pass "${cmd}"
    return 0
  fi
}

test_ping() {
  # Google Public DNS server
  ping_dst 8.8.8.8

  # OpenDNS
  ping_dst 208.67.222.222
}

test_dns() {
  # Use ping until nslookup is ready
  ping_dst www.google.com
  ping_dst fuchsia.com
}

get_file_size() {
  filepath="$1"
  ls_output=$(ls -l "${filepath}")
  filesize=$(echo "${ls_output}" | tr -s '[:space:]' | cut -f5 -d " ")
  echo "${filesize}"
}

wget_dst() {
  tmp_file="/tmp/wlan_smoke_wget.tmp"
  dst="$1"
  bytes_want="$2"

  # Fuchsia Dash's pipe and redirection is funky when used in $(..)
  cmd="wget ${dst} > ${tmp_file}"
  wget "${dst}" > "${tmp_file}"

  bytes_got=$(get_file_size "${tmp_file}")
  if [ "${bytes_got}" -lt "${bytes_want}" ]; then
    log_fail "${cmd}"
  else
    log_pass "${cmd}"
  fi
}


test_wget() {
  wget_dst www.google.com 40000
  wget_dst example.com 1400
}

check_wlan_status() {
  status=$(wlan status | head -n 1 | cut -f 2 -d':' | tr -d '[:space:]')
  echo "${status}"
}

wlan_disconnect() {
  WLAN_STATUS_QUERY_PERIOD=2
  WLAN_STATUS_QUERY_RETRY_MAX=10
  for i in $(seq 1 ${WLAN_STATUS_QUERY_RETRY_MAX}); do
    status=$(check_wlan_status)
    if [ "${status}" = "scanning" ]; then
      log_pass "disconnect"
      return 0
    fi

    log "attempting to disconnect (${i} / ${WLAN_STATUS_QUERY_RETRY_MAX})"
    wlan disconnect > /dev/null
    sleep ${WLAN_STATUS_QUERY_PERIOD}
  done
  log_fail "fails to disconnect"
  test_teardown
  return 1
}

wlan_connect() {
  WLAN_STATUS_QUERY_PERIOD=5
  WLAN_STATUS_QUERY_RETRY_MAX=10

  ssid=$1
  for i in $(seq 1 ${WLAN_STATUS_QUERY_RETRY_MAX}); do
    status=$(check_wlan_status)
    if [ "${status}" = "associated" ]; then
      log_pass "connect to ${ssid}"
      return 0
    fi

    log "attempting to connect to ${ssid} (${i} / ${WLAN_STATUS_QUERY_RETRY_MAX})"
    wlan connect ${ssid} > /dev/null
    sleep ${WLAN_STATUS_QUERY_PERIOD}
  done

  log_fail "fails to connect to ${ssid}"
  test_teardown
  return 1
}

get_eth_iface_list() {
  # The delimiter used in ifconfig is a single tab character
  # TODO(eyw): Verify the behavior of pipe vs file regarding tab vs spaces
  return $(ifconfig | grep ^en | cut -f1 -d$'\t')
}

test_setup() {
  rm -rf "${TEST_LOG}" > /dev/null
  eth_iface_setup=("$@")
  for eth_iface in "${eth_iface_setup[@]}"
  do
    ifconfig "${eth_iface}" down
  done
}

test_teardown() {
  eth_iface_teardown=("$@")
  # Restore to the original state
  for eth_iface in "${eth_iface_teardown[@]}"
  do
    ifconfig "${eth_iface}" up
  done
}

run() {
  cmd="$*"
  set -e
  ${cmd}
  if [ "$?" -ne 0 ]; then
    log_fail "failed in ${cmd}"
    test_teardown
    return 1
  fi
  set +e
}

main() {
  log "Start"
  eth_iface_list=$(get_eth_iface_list)
  run test_setup "${eth_iface_list[@]}"
  run wlan_disconnect
  run wlan_connect GoogleGuest
  log "Starting traffic tests"
  run test_ping
  run test_dns
  run test_wget
  log "Ending traffic tests"
  run wlan_disconnect
  run test_teardown "${eth_iface_list[@]}"
  log "End"
}

main
