# GAP PTS Instructions

## Setup
Tools used to pass GAP tests in PTS:
* ACTS (see \<fuchsia_root\>/src/connectivity/bluetooth/docs/pts/acts_pts_setup.md)
    * Use the BluetoothCmdLineTest tool for all tests:
        * act.py -c \<config\> -tc BluetoothCmdLineTest
* SL4F (see \<fuchsia_root\>/garnet/bin/sl4f/README.md)

## IXIT Values
TSPX_iut_device_name_in_adv_packaet_for_random_address: fs_test

## TESTS

### GAP/DISC/NONM/BV-01-C
ACTS Steps:
1. [PTS Interaction] - Press Yes for the ability to send a non-connectable advertisement
2. `ble_start_generic_nonconnectable_advertisement`
3. `ble_stop_advertisement`

### GAP/DISC/NONM/BV-02-C

### GAP/DISC/GENM/BV-01-C
ACTS Steps:
1. [PTS Interaction] - Press Yes for the ability to send a non-connectable advertisement
2. `ble_start_generic_nonconnectable_advertisement`
3. [PTS Interaction] - Press OK
4. `ble_stop_advertisement`

### GAP/DISC/GENM/BV-02-C
ACTS Steps:
1. [PTS Interaction] - Press Yes for the ability to send a non-connectable advertisement
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Press OK
4. `ble_stop_advertisement`

### GAP/DISC/GENP/BV-01-C
ACTS Steps:
1. [PTS Interaction] Press OK
2. `btc_request_discovery true` (wait some time for scan results)
3. `btc_request_discovery false`
4. `btc_get_known_remote_devices`
5. [PTS Interaction] - Validate PTS is found in the scan results and press OK

### GAP/DISC/GENP/BV-02-C
ACTS Steps:
1. [PTS Interaction] Press OK
2. `btc_request_discovery true` (wait some time for scan results)
3. `btc_request_discovery false`
4. `btc_get_known_remote_devices`
5. [PTS Interaction] - Validate PTS is found in the scan results and press OK

### GAP/DISC/GENP/BV-03-C
ACTS Steps:
1. [PTS Interaction] Press OK
2. `btc_request_discovery true` (wait some time for scan results)
3. `btc_request_discovery false`
4. `btc_get_known_remote_devices`
5. [PTS Interaction] - Validate PTS is not found in the scan results and press OK

### GAP/DISC/GENP/BV-04-C
ACTS Steps:
1. [PTS Interaction] Press OK
2. `btc_request_discovery true` (wait some time for scan results)
3. `btc_request_discovery false`
4. `btc_get_known_remote_devices`
5. [PTS Interaction] - Validate PTS is not found in the scan results and press OK

### GAP/DISC/GENP/BV-05-C
ACTS Steps:
1. [PTS Interaction] Press Yes
2. `btc_request_discovery true` (wait some time for scan results)
3. `btc_request_discovery false`
4. `btc_get_known_remote_devices`
5. [PTS Interaction] - Validate PTS is not found in the scan results and press OK

### GAP/IDLE/NAMP/BV-01-C
Note: Run GAP/CONN/GCEP/BV-02-C first to get the unique id needed.

ACTS Steps:
1. [PTS Interaction] Press Yes
2. `tool_refresh_unique_id_using_bt_control`
3. `gattc_connect`
4. `gattc_read_all_chars`
5. `gattc_disconnect`

### GAP/IDLE/NAMP/BV-02-C
Note: Run GAP/CONN/GCEP/BV-02-C first to get the unique id needed.

ACTS Steps:
1. [PTS Interaction] Press Yes
2. `tool_refresh_unique_id_using_bt_control`
3. `gattc_connect`

### GAP/IDLE/GIN/BV-01-C
ACTS Steps:
1. `tool_refresh_unique_id_using_bt_control`
2. [PTS Interaction] Press Yes if peer id is found

### GAP/IDLE/DED/BV-02-C
ACTS Steps:
1. [PTS Interaction] Press Ok/Yes
2. `tool_refresh_unique_id_using_bt_control`
3. [PTS Interaction] Press yes if peer ID was found based on the given name

### GAP/CONN/NCON/BV-01-C
ACTS Steps:
1. [PTS Interaction] Press Ok/Yes
2. `ble_start_generic_nonconnectable_advertisement`

### GAP/CONN/NCON/BV-02-C
ACTS Steps:
1. [PTS Interaction] Press No
2. `ble_start_generic_nonconnectable_advertisement`

### GAP/CONN/UCON/BV-01-C
ACTS Steps:
1. `ble_start_generic_connectable_advertisement`

### GAP/CONN/UCON/BV-02-C
ACTS Steps:
1. `ble_start_generic_connectable_advertisement`

### GAP/CONN/ACEP/BV-01-C
Note: Run GAP/CONN/GCEP/BV-02-C first to get the unique id needed. Send outgoing connection very quickly after the test starts.
Note: Send the connection request very quickly to PTS after the test starts
Pre-steps:
Replace TSPX_bd_addr_iut with the random address of the Fuchsia device.
This can be one by:
1. Advertising on Android with nrf connect, connecting to it with the Fuchsia device and then inputting the address to PTS before running the test. This is tedious but works.

ACTS Steps:
1. `gattc_connect`
2. `gattc_disconnect`

### GAP/CONN/GCEP/BV-01-C
ACTS Steps:
1. `tool_refresh_unique_id`
2. `gattc_connect`
3. `gattc_disconnect`

### GAP/CONN/GCEP/BV-02-C
Note: Run GAP/CONN/GCEP/BV-02-C first to get the unique id needed. Send outgoing connection very quickly after the test starts.
Pre-steps:
Replace TSPX_bd_addr_iut with the random address of the Fuchsia device.
This can be one by:
1. Advertising on Android with nrf connect, connecting to it with the Fuchsia device and then inputting the address to PTS before running the test. This is tedious but works.

ACTS Steps:
1. `gattc_connect`
2. `gattc_disconnect`


### GAP/CONN/DCEP/BV-01-C
Note: Run GAP/CONN/GCEP/BV-02-C first to get the unique id needed. Send outgoing connection very quickly after the test starts.
Pre-steps:
Replace TSPX_bd_addr_iut with the random address of the Fuchsia device.
This can be one by:
1. Advertising on Android with nrf connect, connecting to it with the Fuchsia device and then inputting the address to PTS before running the test. This is tedious but works.

ACTS Steps:
1. `gattc_connect`
2. `gattc_disconnect`

### GAP/CONN/DCEP/BV-03-C
Note: Run GAP/CONN/GCEP/BV-02-C first to get the unique id needed. Send outgoing connection very quickly after the test starts.
Pre-steps:
Replace TSPX_bd_addr_iut with the random address of the Fuchsia device.
This can be one by:
1. Advertising on Android with nrf connect, connecting to it with the Fuchsia device and then inputting the address to PTS before running the test. This is tedious but works.

ACTS Steps:
1. `gattc_connect`
2. `gattc_disconnect`

### GAP/CONN/CPUP/BV-04-C
Note: Run GAP/CONN/GCEP/BV-02-C first to get the unique id needed. Send outgoing connection very quickly after the test starts.
Pre-steps:
Replace TSPX_bd_addr_iut with the random address of the Fuchsia device.
This can be one by:
1. Advertising on Android with nrf connect, connecting to it with the Fuchsia device and then inputting the address to PTS before running the test. This is tedious but works.

ACTS Steps:
1. `gattc_connect`
2. `gattc_disconnect`


### GAP/CONN/CPUP/BV-05-C
Note: Run GAP/CONN/GCEP/BV-02-C first to get the unique id needed. Send outgoing connection very quickly after the test starts.
Pre-steps:
Replace TSPX_bd_addr_iut with the random address of the Fuchsia device.
This can be one by:
1. Advertising on Android with nrf connect, connecting to it with the Fuchsia device and then inputting the address to PTS before running the test. This is tedious but works.

ACTS Steps:
1. `gattc_connect`
2. `gattc_disconnect`

### GAP/CONN/CPUP/BV-06-C
Note: Run GAP/CONN/GCEP/BV-02-C first to get the unique id needed. Send outgoing connection very quickly after the test starts.
Pre-steps:
Replace TSPX_bd_addr_iut with the random address of the Fuchsia device.
This can be one by:
1. Advertising on Android with nrf connect, connecting to it with the Fuchsia device and then inputting the address to PTS before running the test. This is tedious but works.

ACTS Steps:
1. `gattc_connect`
2. `gattc_disconnect`

### GAP/CONN/TERM/BV-01-C
Note: Run GAP/CONN/GCEP/BV-02-C first to get the unique id needed. Send outgoing connection very quickly after the test starts.
Pre-steps:
Replace TSPX_bd_addr_iut with the random address of the Fuchsia device.
This can be one by:
1. Advertising on Android with nrf connect, connecting to it with the Fuchsia device and then inputting the address to PTS before running the test. This is tedious but works.

ACTS Steps:
1. [PTS Interaction] - Click Yes to indicate the support of both central and peripheral
1. `gattc_connect`
2. `gattc_disconnect`

### GAP/BOND/NBON/BV-01-C

### GAP/BOND/NBON/BV-02-C

### GAP/BOND/NBON/BV-03-C
Pre-steps:
Replace TSPX_bd_addr_iut with the random address of the Fuchsia device.
This can be one by:
1. Advertising on Android with nrf connect, connecting to it with the Fuchsia device and then inputting the address to PTS before running the test. This is tedious but works.

Note: PTS is buggy, run twice
ACTS Steps:
1. `btc_accept_pairing`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Press OK to disconnect

### GAP/BOND/BON/BV-01-C
Pre-steps:
Note: Run the test a first time and it will fail. Change the TSPX_bd_addr_iut ixit value address to be the peer address in the PTS logs that start with:
  SEC_LE?SEC_LE_REMOTE_CSRK_REQUEST_IND=PDU
    peerAddr: 'xxxxxxxxxxxx'O

ACTS Steps:
1. `btc_accept_pairing`
2. `ble_start_generic_connectable_advertisement`
3. `btc_get_known_remote_devices` - Save peer 'id' from the response
4. tool_set_unique_mac_addr_id <'id from step 3'>
5. [PTS Interaction] - Press OK to disconnect
6. `ble_start_generic_connectable_advertisement`

### GAP/BOND/BON/BV-02-C
Note: PTS is buggy
Pre-steps:
Run the test a first time and it will fail. Change the TSPX_bd_addr_iut ixit value address to be the peer address in the PTS logs that start with:
  SEC_LE?SEC_LE_REMOTE_CSRK_REQUEST_IND=PDU
    peerAddr: 'xxxxxxxxxxxx'O
Also verify that neither PTS or Fuchsia has no bonded devices before the test is run.

ACTS Steps:
1. `btc_accept_pairing`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `btc_pair ENCRYPTED BONDABLE LE`
5. [PTS Interaction] - Press OK to disconnect the link
6. `gattc_connect`
7. [PTS Interaction] - Press OK to disconnect the link

### GAP/BOND/BON/BV-03-C
Note: PTS is buggy, run twice
ACTS Steps:
1. `btc_accept_pairing`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Press OK to disconnect
4. `ble_start_generic_connectable_advertisement`
5. [PTS Interaction] - Press OK to disconnect
6. `ble_start_generic_connectable_advertisement`
7. [PTS Interaction] - Press OK to disconnect

### GAP/BOND/BON/BV-04-C
Note: PTS is buggy
Pre-steps:
Run the test a first time and it will fail. Change the TSPX_bd_addr_iut ixit value address to be the peer address in the PTS logs that start with:
  SEC_LE?SEC_LE_REMOTE_CSRK_REQUEST_IND=PDU
    peerAddr: 'xxxxxxxxxxxx'O
Also verify that neither PTS or Fuchsia has no bonded devices before the test is run.

ACTS Steps:This can be one by:
1. `btc_accept_pairing`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_disconnect` - When prompted
4. `gattc_connect`
5. [PTS Interaction] - Press OK to disconnect the link


### GAP/SEC/AUT/BV-02-C
### GAP/SEC/AUT/BV-11-C
ACTS Steps:
1. `gatts_setup_database TEST_DB_2`
2. `btc_set_io_capabilities KEYBOARD DISPLAY`
3. `btc_accept_pairing`
4. `ble_start_generic_connectable_advertisement`
5. [PTS Interaction] - input 0x000e
6. `btc_input_pairing_pin [pin]` - Pin from PTS prompt

### GAP/SEC/AUT/BV-12-C
Pre-steps:
Run the test a first time and it will fail. Change the TSPX_bd_addr_iut ixit value address to be the peer address in the PTS logs that start with:
  SEC_LE?SEC_LE_REMOTE_CSRK_REQUEST_IND=PDU
    peerAddr: 'xxxxxxxxxxxx'O
Also verify that neither PTS or Fuchsia has no bonded devices before the test is run.

ACTs Steps
1. `tool_refresh_unique_id`
2. `btc_accept_pairing`
3. `gatts_setup_database TEST_DB_2`
4. `gattc_connect`
5. [PTS Interaction] - Enter the handle 000e into PTS for the characteristic handle with insufficient authentication
6. bt-cli> pair <id> ENC T LE

### GAP/SEC/AUT/BV-13-C

### GAP/SEC/AUT/BV-14-C

### GAP/SEC/AUT/BV-17-C
Pre-steps:
Run the test a first time and it will fail. Change the TSPX_bd_addr_iut ixit value address to be the peer address in the PTS logs that start with:
  SEC_LE?SEC_LE_REMOTE_CSRK_REQUEST_IND=PDU
    peerAddr: 'xxxxxxxxxxxx'O
Also verify that neither PTS or Fuchsia has no bonded devices before the test is run.

ACTs Steps
1. `tool_refresh_unique_id`
2. `btc_accept_pairing`
3. `gattc_connect`
4. `gattc_read_char_by_id 0009`
6. bt-cli> pair <id> ENC T LE

### GAP/SEC/AUT/BV-18-C
Pre-steps:
Run the test a first time and it will fail. Change the TSPX_bd_addr_iut ixit value address to be the peer address in the PTS logs that start with:
  SEC_LE?SEC_LE_REMOTE_CSRK_REQUEST_IND=PDU
    peerAddr: 'xxxxxxxxxxxx'O
Also verify that neither PTS or Fuchsia has no bonded devices before the test is run.

ACTS Steps:
1. `btc_accept_pairing`
2. `ble_start_generic_connectable_advertisement`
3. `btc_get_known_remote_devices` - Save peer 'id' from the response
4. tool_set_unique_mac_addr_id <'id from step 3'>
5. `gattc_connect`
6. `gattc_read_char_by_id` [id from PTS]

### GAP/SEC/AUT/BV-19-C
Pre-steps:
Run the test a first time and it will fail. Change the TSPX_bd_addr_iut ixit value address to be the peer address in the PTS logs that start with:
  SEC_LE?SEC_LE_REMOTE_CSRK_REQUEST_IND=PDU
    peerAddr: 'xxxxxxxxxxxx'O
Also verify that neither PTS or Fuchsia has no bonded devices before the test is run.

ACTS Steps:
1. `btc_accept_pairing`
2. `tool_refresh_unique_id`
2. `gattc_connect`
4. `btc_pair ENCRYPTED BONDABLE LE`
5. [PTS Interaction] - Press OK to disconnect link
6. While disconnected `btc_forget_all_known_devices`
7. `tool_refresh_unique_id`
8. `gattc_connect`
9. `gattc_read_char_by_id [id]` - ID provided by PTS
10. `gattc_read_char_by_id [id]` - ID provided by PTS

### GAP/SEC/AUT/BV-20-C
Pre-steps:
Run the test a first time and it will fail. Change the TSPX_bd_addr_iut ixit value address to be the peer address in the PTS logs that start with:
  SEC_LE?SEC_LE_REMOTE_CSRK_REQUEST_IND=PDU
    peerAddr: 'xxxxxxxxxxxx'O
Also verify that neither PTS or Fuchsia has no bonded devices before the test is run.
Note: This test is pretty broken as it doesn't respond to security upgrades after the read char command. bt-cli will have to be used in conjunction.

ACTS Steps:
1. Run bt-cli in another window
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`
4. `ble_start_generic_connectable_advertisement`
5. `btc_get_known_remote_devices` - Save peer 'id' from the response
6. tool_set_unique_mac_addr_id <'id from step 5'>
7. `gattc_connect`
8. `gattc_read_char_by_id [id]` - ID provided by PTS
9. bt-cli> pair <id> ENC T LE
10. `gattc_read_char_by_id [id]` - ID provided by PTS
11. bt-cli> pair <id> ENC T LE

### GAP/SEC/AUT/BV-21-C
Pre-steps:
Run the test a first time and it will fail. Change the TSPX_bd_addr_iut ixit value address to be the peer address in the PTS logs that start with:
  SEC_LE?SEC_LE_REMOTE_CSRK_REQUEST_IND=PDU
    peerAddr: 'xxxxxxxxxxxx'O
Also verify that neither PTS or Fuchsia has no bonded devices before the test is run.
Note: Very finiky test

ACTS Steps:
1. `btc_accept_pairing`
2. `tool_refresh_unique_id`
2. `gattc_connect`
4. `btc_pair ENCRYPTED BONDABLE LE`
5. [PTS Interaction] - Press OK to disconnect link
6. `gattc_connect`


### GAP/SEC/AUT/BV-22-C
Pre-steps:
Run the test a first time and it will fail. Change the TSPX_bd_addr_iut ixit value address to be the peer address in the PTS logs that start with:
  SEC_LE?SEC_LE_REMOTE_CSRK_REQUEST_IND=PDU
    peerAddr: 'xxxxxxxxxxxx'O
Also verify that neither PTS or Fuchsia has no bonded devices before the test is run.
ACTS Steps:
1. `btc_accept_pairing`
2. `ble_start_generic_connectable_advertisement`
3. `ble_start_generic_connectable_advertisement`
4. `btc_get_known_remote_devices` - Save peer 'id' from the response
5. tool_set_unique_mac_addr_id <'id from step 4'>
6. `btc_pair ENCRYPTED BONDABLE LE`
7. [PTS Interaction] - Verify value and press OK

### GAP/SEC/AUT/BV-23-C
Pre-steps:
Run the test a first time and it will fail. Change the TSPX_bd_addr_iut ixit value address to be the peer address in the PTS logs that start with:
  SEC_LE?SEC_LE_REMOTE_CSRK_REQUEST_IND=PDU
    peerAddr: 'xxxxxxxxxxxx'O
Also verify that neither PTS or Fuchsia has no bonded devices before the test is run.
Note: Secure connections only mode must be enabled.
ACTS Steps:
1. `btc_set_io_capability KEYBOARD DISPLAY`
2. `btc_accept_pairing`
3. `gatts_setup_database TEST_DB_2`
4. `ble_start_generic_connectable_advertisement`
5. `ble_start_generic_connectable_advertisement`
6. [PTS Interaction] - Enter 000e for the handle with insufficient authentication

### GAP/SEC/AUT/BV-24-C
Pre-steps:
Run the test a first time and it will fail. Change the TSPX_bd_addr_iut ixit value address to be the peer address in the PTS logs that start with:
  SEC_LE?SEC_LE_REMOTE_CSRK_REQUEST_IND=PDU
    peerAddr: 'xxxxxxxxxxxx'O
Also verify that neither PTS or Fuchsia has no bonded devices before the test is run.
Note: Secure connections only mode must be enabled.
Note: This test is a bit flakey (sometimes stalls)
ACTS Steps:
1. `btc_set_io_capability KEYBOARD DISPLAY`
2. `btc_accept_pairing`
3. `gatts_setup_database TEST_DB_2`
4. `tool_refresh_unique_id`
5. `gattc_connect`
6. `gattc_disconnect`
7. `gattc_connect`
8. [PTS Interaction] - Enter 000e for the handle with insufficient authentication
9. `btc_get_pairing_pin` (run until you get a full 6 digit key. the first pairing attempt may populate the first call with just 0)
10. [PTS Interaction] - Enter the pin from step 9 into PTS

### GAP/SEC/SEM/BI-04-C
### GAP/SEC/SEM/BI-08-C
### GAP/SEC/SEM/BI-09-C
### GAP/SEC/SEM/BI-10-C
### GAP/SEC/SEM/BV-04-C
### GAP/SEC/SEM/BV-05-C
### GAP/SEC/SEM/BV-06-C
### GAP/SEC/SEM/BV-07-C

### GAP/SEC/SEM/BV-08-C
Tentative steps:
Set PSM in IXIT to 0001
1. `btc_set_io_capabilities KEYBOARD DISPLAY`
2. `btc_accept_pairing`
3. [PTS Interaction] - Press OK
4. `btc_connect`

### GAP/SEC/SEM/BV-09-C
### GAP/SEC/SEM/BV-10-C
### GAP/SEC/SEM/BV-21-C
### GAP/SEC/SEM/BV-22-C
### GAP/SEC/SEM/BV-23-C
### GAP/SEC/SEM/BV-24-C
### GAP/SEC/SEM/BV-26-C
### GAP/SEC/SEM/BV-27-C
### GAP/SEC/SEM/BV-28-C
### GAP/SEC/SEM/BV-29-C

### GAP/ADV/BV-01-C
Note: LE advertisement address must match in TSPX bd_addr_iut. Use a second device to find the LE advertisement address

### GAP/ADV/BV-02-C
Note: LE advertisement address must match in TSPX bd_addr_iut. Use a second device to find the LE advertisement address

### GAP/ADV/BV-03-C
Note: LE advertisement address must match in TSPX bd_addr_iut. Use a second device to find the LE advertisement address

### GAP/ADV/BV-04-C
Note: LE advertisement address must match in TSPX bd_addr_iut. Use a second device to find the LE advertisement address

### GAP/ADV/BV-05-C
Note: LE advertisement address must match in TSPX bd_addr_iut. Use a second device to find the LE advertisement address

### GAP/ADV/BV-10-C
Note: LE advertisement address must match in TSPX bd_addr_iut. Use a second device to find the LE advertisement address

### GAP/ADV/BV-11-C
Note: LE advertisement address must match in TSPX bd_addr_iut. Use a second device to find the LE advertisement address


### GAP/DM/CON/BV-01-C
ACTS Steps:
1. `btc_set_discoverable true`
2. [PTS Interaction] - Press OK

### GAP/DM/BON/BV-01-C

1. `btc_accept_pairing`
2. `btc_set_discoverable true`
3. `btc disconnect`
4. `tool_refrsh_unique_id`
5. `gattc_connect`
6. `btc_pair ENCRYPTED BONDABLE LE`
7. [PTS Interaction] - Press OK to disconnect
8. `gattc_connect`
9. [PTS Interaction] - Press OK to disconnect

### GAP/DM/GIN/BV-01-C
ACTS Steps:
1. `tool_refresh_unique_id_using_bt_control`
2. `tool_refresh_unique_id`
3. [PTS Interaction] - Verify steps 1 and 2 returned IDs and press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `tool_refresh_unique_id`
6. [PTS Interaction] - Press OK

### GAP/DM/NAD/BV-01-C
ACTS Steps:
1. `tool_refresh_unique_id_using_bt_control`
2. [PTS Interaction] - Verify ID found and press OK.

### GAP/DM/NAD/BV-02-C
ACTS Steps:
1. `tool_refresh_unique_id`
2. `gattc_connect`
3. `gattc_read_all_chars`
4. `gattc_disconnect`

### GAP/DM/LEP/BV-01-C
Note: Put public address in IXIT
ACTS Steps:
1. `btc_set_discoverable_true`
2. [PTS Interaction] - Press OK
3. `ble_start_generic_connectable_advertisement`
[TBD on the rest, still a failure]

### GAP/DM/LEP/BV-02-C
ACTS Steps:
1. `btc_set_discoverable_true`
2. [PTS Interaction] - Press OK
3. `ble_start_generic_connectable_advertisement`

### GAP/DM/LEP/BV-04-C
### GAP/DM/LEP/BV-05-C

### GAP/DM/LEP/BV-06-C
ACTS Steps:
1. `tool_refresh_unique_id`
2. `gattc_connect`

### GAP/DM/LEP/BV-07-C
ACTS Steps:
1. `ble_start_generic_connectable_advertisement`

### GAP/DM/LEP/BV-08-C
ACTS Steps:
1. `ble_start_generic_connectable_advertisement`

### GAP/DM/LEP/BV-09-C


### GAP/DM/LEP/BV-11-C
ACTS Steps:
1. `btc_set_discoverable_true`
2. [PTS Interaction] - Press OK
3. `tool_refresh_unique_id`
4. `gattc_connect`
5. `gattc_disconnect`

### GAP/MOD/NDIS/BV-01-C
ACTS Steps:
None. Wait 30 seconds.

### GAP/MOD/GDIS/BV-01-C
ACTS Steps:
1. `btc_set_discoverable_true`
2. [PTS Interaction] - Press OK

### GAP/MOD/GDIS/BV-02-C
ACTS Steps:
1. `btc_set_discoverable_true`
2. [PTS Interaction] - Press OK

### GAP/MOD/CON/BV-01-C
ACTS Steps:
1. `btc_set_discoverable_true`
2. [PTS Interaction] - Press OK

### GAP/EST/LIE/BV-02-C
### GAP/IDLE/BON/BV-05-C
### GAP/IDLE/BON/BV-06-C
### GAP/IDLE/DNDIS/BV-01-C
