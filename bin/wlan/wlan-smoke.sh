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

TEST_LOG="$1"
[ -z ${TEST_LOG} ] && TEST_LOG="/tmp/wlan-doctor.log"

WLAN_DISCONNECT_QUERY_PERIOD=2 # seconds
WLAN_DISCONNECT_QUERY_RETRY_MAX=10
WLAN_CONNECT_QUERY_PERIOD=5 # seconds
WLAN_CONNECT_QUERY_RETRY_MAX=10
DHCP_QUERY_RETRY_PERIOD=1 # seconds
DHCP_QUERY_RETRY_MAX=5

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
    return 1
  else
    log_pass "${cmd}"
    return 0
  fi
}

test_ping() {
  # Google Public DNS server
  ping_dst 8.8.8.8 "$@"

  # OpenDNS
  ping_dst 208.67.222.222 "$@"
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

curl_md5sum() {
  url="$1"
  tmp_file="/tmp/wlan_smoke_md5sum.tmp"
  md5_wanted="$2"

  speed=$(curl -sw "%{speed_download}" -o "${tmp_file}" "${url}" | cut -f 1 -d ".")
  speed=$((speed/1024))
  md5_download=$(md5sum "${tmp_file}" | cut -f 1 -d " ")

  msg="curl_md5sum ${speed}kB/s ${url}"
  if [ "${md5_download}" = "${md5_wanted}" ]; then
    log_pass "${msg}"
  else
    log_fail "${msg}"
  fi
}

test_curl_md5sum() {
  curl_md5sum http://ovh.net/files/1Mb.dat 62501d556539559fb422943553cd235a
  # curl_md5sum http://ovh.net/files/1Mio.dat 6cb91af4ed4c60c11613b75cd1fc6116
  # curl_md5sum http://ovh.net/files/10Mb.dat 241cead4562ebf274f76f2e991750b9d
  # curl_md5sum http://ovh.net/files/10Mio.dat ecf2a421f46ab33f277fa2aaaf141780
  # curl_md5sum http://ipv4.download.thinkbroadband.com/5MB.zip b3215c06647bc550406a9c8ccc378756
}

check_wlan_status() {
  status=$(wlan status | head -n 1 | cut -f 2 -d':' | tr -d '[:space:]')
  echo "${status}"
}

wlan_disconnect() {
  for i in $(seq 1 ${WLAN_DISCONNECT_QUERY_RETRY_MAX}); do
    status=$(check_wlan_status)
    if [ "${status}" = "scanning" ]; then
      log_pass "disconnect"
      return 0
    fi

    log "attempting to disconnect (${i} / ${WLAN_DISCONNECT_QUERY_RETRY_MAX})"
    wlan disconnect > /dev/null
    sleep ${WLAN_DISCONNECT_QUERY_PERIOD}
  done
  log_fail "fails to disconnect"
  return 1
}

wlan_connect() {
  ssid=$1
  for i in $(seq 1 ${WLAN_CONNECT_QUERY_RETRY_MAX}); do
    status=$(check_wlan_status)
    if [ "${status}" = "associated" ]; then
      log_pass "connect to ${ssid}"
      return 0
    fi

    log "attempting to connect to ${ssid} (${i} / ${WLAN_CONNECT_QUERY_RETRY_MAX})"
    wlan connect "${ssid}" > /dev/null
    sleep ${WLAN_CONNECT_QUERY_PERIOD}
  done

  log_fail "fails to connect to ${ssid}"
  return 1
}

wait_for_dhcp() {
  for i in $(seq 1 ${DHCP_QUERY_RETRY_MAX}); do
    inet_addr=$(get_wlan_inet_addr)
    if [ ! -z "${inet_addr}" ]; then
      log_pass "dhcp address: ${inet_addr}"
      return 0
    fi
    sleep "${DHCP_QUERY_RETRY_PERIOD}"
  done
}

get_wlan_inet_addr() {
  wlan_iface=$(ifconfig | grep ^wlan | tr '[:blank:]' ' ' | cut -f1 -d' ')
  wlan_inet_addr=$(ifconfig $wlan_iface | grep "inet addr" | cut -f2 -d':' | cut -f1 -d' ' | grep ".")
  echo "${wlan_inet_addr}"
}

get_eth_iface_list() {
  iface_list=$(ifconfig | grep ^en | cut -f1 -d'\t')
  echo "${iface_list}"
}

test_setup() {
  rm -rf "${TEST_LOG}" > /dev/null
  for eth_iface in "$@"
  do
    ifconfig "${eth_iface}" down
  done
}

test_teardown() {
  # Restore to the original state
  for eth_iface in "$@"
  do
    ifconfig "${eth_iface}" up
  done
}

run() {
  cmd="$*"
  ${cmd}
  if [ "$?" -ne 0 ]; then
    log_fail "failed in ${cmd}"
    return 1
  fi
}

main() {
  log "Start"
  eth_iface_list=$(get_eth_iface_list)
  run test_setup "${eth_iface_list}"
  run wlan_disconnect
  run wlan_connect GoogleGuest
  run wait_for_dhcp
  log "Starting traffic tests"
  run test_ping
  run test_dns
  run test_curl_md5sum
  log "Ending traffic tests"
  run wlan_disconnect
  run test_teardown "${eth_iface_list}"
  log "End"
}

main
