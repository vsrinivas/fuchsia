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
  filesize=$(echo "${ls_output}" | tr '\t' ' ' | cut -f5 -d " ")
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

test_wlan_association() {
  # Confirm if WLAN interface is associated (and RSN-authenticated)
  WLAN_STATUS_QUERY_PERIOD=5
  WLAN_STATUS_QUERY_RETRY_MAX=10
  for i in $(seq 1 ${WLAN_STATUS_QUERY_RETRY_MAX}); do
    status=$(check_wlan_status)
    if [ "${status}" = "associated" ]; then
      wlan_network_info=$(wlan status | tail -n 1)
      log_pass "associated to ${wlan_network_info}"
      return 0
    fi
    log "querying WLAN status (${i} / ${WLAN_STATUS_QUERY_RETRY_MAX})"
    sleep ${WLAN_STATUS_QUERY_PERIOD}
  done
  log_fail "fails to get associated"
  test_teardown
  return 1
}

get_eth_iface_name() {
  # There exists no way reliably to figure out this in Fuchsia.
  # Return hard-coded value, noting WLAN developers setups are similar.
  # Godspeed.
  echo "en2"
}

test_setup() {
  rm -rf "${TEST_LOG}"
  eth_iface=$(get_eth_iface_name)
  ifconfig "${eth_iface}" down
}

test_teardown() {
  # Restore to the original state
  eth_iface=$(get_eth_iface_name)
  ifconfig "${eth_iface}" up
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
  run test_setup
  run test_wlan_association
  run test_ping
  run test_dns
  run test_wget
  run test_teardown
  log "End"
}

main
