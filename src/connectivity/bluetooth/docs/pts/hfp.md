# HFP PTS Instructions

## Setup
Tools used to pass HFP tests in PTS:
* ACTS (see \<fuchsia_root\>/src/connectivity/bluetooth/docs/pts/acts_pts_setup.md)
    * Use the BluetoothCmdLineTest tool for all tests:
        * act.py -c \<config\> -tc BluetoothCmdLineTest
* SL4F (see \<fuchsia_root\>/src/testing/sl4f/README.md)

## IXIT Values
TSPX_security_enabled = True
TSPX_phone_number = 1234567890 #Replace with your phone number

## TESTS

### HFP/AG/OOR/BV-02-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
9. [PTS Interaction] - Press OK
<TBD>

### HFP/AG/TRS/BV-01-C
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_set_service_available false`
9. [PTS Interaction] - Press OK
10. `hfp_set_service_available true`
11. [PTS Interaction] - Press OK
12. `btc_disconnect`

### HFP/AG/PSI/BV-01-C
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. `hfp_set_signal_strength 5`
5. [PTS Interaction] - Press OK
6. `tool_refresh_unique_id_using_bt_control`
7. `btc_connect`
8. [PTS Interaction] - Press OK
9. `hfp_set_signal_strength 0`
10. [PTS Interaction] - Press OK
11. [PTS Interaction] - Press OK
12. `btc_disconnect`

### HFP/AG/PSI/BV-03-C
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. `hfp_set_battery_level 1`
8. [PTS Interaction] - Press OK
9. `hfp_set_battery_level 3`
10. [PTS Interaction] - Press OK
11. [PTS Interaction] - Press OK
12. `btc_disconnect`

### HFP/AG/PSI/BV-04-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. [PTS Interaction] - Press OK
9. `btc_disconnect`

### HFP/AG/PSI/BV-05-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. [PTS Interaction] - Press OK
6. `btc_disconnect`

### HFP/AG/ACS/BV-04-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. `btc_disconnect`
12. [PTS Interaction] - Press OK
13. `btc_connect`
<TBD>

### HFP/AG/ACS/BV-08-I

### HFP/AG/ACS/BV-11-I


### HFP/AG/ACS/BI-14-I

### HFP/AG/ACS/BV-16-I

### HFP/AG/ACR/BV-01-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. `hfp_list_calls` // Use latest call number from this list
12. `hfp_set_call_terminated #` // Number from previous step
13. [PTS Interaction] - Press OK
14. `btc_disconnect`

### HFP/AG/ACR/BV-02-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. `hfp_list_calls` // Use latest call number from this list
12. `hfp_set_call_terminated #` // Number from previous step
13. [PTS Interaction] - Press OK
14. `btc_disconnect`

### HFP/AG/CLI/BV-01-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. `hfp_list_calls` // Use latest call number from this list
12. `hfp_set_call_terminated #` // Number from previous step
13. [PTS Interaction] - Press OK
14. `btc_disconnect`

### HFP/AG/ICA/BV-04-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. `hfp_list_calls` // Use latest call number from this list
12. `hfp_set_call_terminated #` // Number from previous step
13. [PTS Interaction] - Press OK
14. `btc_disconnect`

### HFP/AG/ICA/BV-06-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. `hfp_list_calls` // Use latest call number from this list
12. `hfp_set_call_terminated #` // Number from previous step
13. [PTS Interaction] - Press OK
14. `btc_disconnect`

### HFP/AG/TCA/BV-01-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
9. [PTS Interaction] - Press OK
10. `btc_disconnect`

### HFP/AG/TCA/BV-02-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. `hfp_list_calls` // Use latest call number from this list
12. `hfp_set_call_terminated #` // Number from previous step
13. [PTS Interaction] - Press OK
14. `btc_disconnect`

### HFP/AG/TCA/BV-03-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. `hfp_list_calls` // Use latest call number from this list
12. `hfp_set_call_terminated #` // Number from previous step
13. [PTS Interaction] - Press OK
14. `btc_disconnect`

### HFP/AG/TCA/BV-04-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
9. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678981`
<TBD>

### HFP/AG/TCA/BV-05-I

### HFP/AG/ATH/BV-03-I

### HFP/AG/ATH/BV-04-I

### HFP/AG/ATH/BV-06-I

### HFP/AG/ATA/BV-01-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. `btc_disconnect`
12. [PTS Interaction] - Press OK
13. [PTS Interaction] - Press OK
14. `hfp_list_calls` // Use latest call number from this list
15. `hfp_set_call_terminated #` // Number from previous step
16. `btc_connect`
17. [PTS Interaction] - Press OK
18. `btc_disconnect`

### HFP/AG/ATA/BV-02-I

### HFP/AG/OCN/BV-01-I

### HFP/AG/OCM/BV-01-I


### HFP/AG/OCM/BV-02-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_clear_memory_location #` // If any location exists
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. [PTS Interaction] - Press OK
12. [PTS Interaction] - Press OK
13. `btc_disconnect`

### HFP/AG/OCL/BV-01-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
4. [PTS Interaction] - Press OK
<TBD>

### HFP/AG/OCL/BV-02-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_clear_last_dialed`
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. `btc_disconnect`

### HFP/AG/TWC/BV-02-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
<TBD>

### HFP/AG/TWC/BV-03-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
<TBD>

### HFP/AG/TWC/BV-05-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
4. [PTS Interaction] - Press OK
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
7. [PTS Interaction] - Press OK
<TBD>

### HFP/AG/CIT/BV-01-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
9. [PTS Interaction] - Press OK
10. `list calls`
11. `hfp_set_call_terminated #` // Use latest number from previous step
12. [PTS Interaction] - Press OK
13. `btc_disconnect`

### HFP/AG/ENO/BV-01-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
9. [PTS Interaction] - Press Ok
10. [PTS Interaction] - Press OK
<TBD>

### HFP/AG/TDC/BV-01-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678980`
9. [PTS Interaction] - Press Ok
<TBD>

### HFP/AG/ECS/BV-01-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
8. `hfp_incoming_call 12345678980`
7. [PTS Interaction] - Press OK
8. `hfp_incoming_call 12345678981`
7. [PTS Interaction] - Press OK
<TBD>

### HFP/AG/ECS/BV-02-I
### HFP/AG/ECS/BV-03-I

### HFP/AG/ECC/BI-03-I


### HFP/AG/ECC/BI-04-I

### HFP/AG/NUM/BV-01-I
ACTS Steps:
1. `hfp_remove_service`
2. `btc_accept_pairing`
3. `tool_set_target_device_name PTS`
4. `hfp_init`
5. [PTS Interaction] - Press OK
6. `tool_refresh_unique_id_using_bt_control`
7. `btc_connect`
8. [PTS Interaction] - Press OK
9. `btc_disconnect`

### HFP/AG/SLC/BV-01-C
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `btc_set_discoverable true`
4. `hfp_init`
5. [PTS Interaction] - Press OK
6. [PTS Interaction] - Press OK
7. `tool_refresh_unique_id_using_bt_control`
8. `btc_disconnect`

### HFP/AG/SLC/BV-02-C
Note: Revisit when setting autoconnect on peer is available
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. [PTS Interaction] - Press OK
6. `tool_refresh_unique_id_using_bt_control`
7. `btc_connect`
8. [PTS Interaction] - Press OK
9. `btc_disconnect`

### HFP/AG/SLC/BV-03-C
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `btc_set_discoverable true`
4. `hfp_init`
5. [PTS Interaction] - Press OK
6. [PTS Interaction] - Press OK
7. `tool_refresh_unique_id_using_bt_control`
8. `btc_disconnect`

### HFP/AG/SLC/BV-04-C
Note: Revisit when setting autoconnect on peer is available
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. [PTS Interaction] - Press OK
6. `tool_refresh_unique_id_using_bt_control`
7. `btc_connect`
8. [PTS Interaction] - Press OK
9. `btc_disconnect`

### HFP/AG/SLC/BV-05-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_disconnect`

### HFP/AG/SLC/BV-06-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `btc_disconnect`

### HFP/AG/SLC/BV-07-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_disconnect`


### HFP/AG/SLC/BV-09-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `btc_disconnect`

### HFP/AG/SLC/BV-10-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `btc_disconnect`

### HFP/AG/ACC/BV-08-I

### HFP/AG/ACC/BV-09-I
### HFP/AG/ACC/BV-10-I
### HFP/AG/ACC/BV-11-I
### HFP/AG/ACC/BI-12-I
### HFP/AG/ACC/BI-13-I
### HFP/AG/ACC/BI-14-I
### HFP/AG/ACC/BV-15-I

### HFP/AG/WBS/BV-01-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. <Wait until PTS prompt>
5. `tool_refresh_unique_id_using_bt_control`
6. [PTS Interaction] - Press OK
7. `btc_disconnect`


### HFP/AG/DIS/BV-01-I
ACTS Steps:
1. `btc_forget_all_known_devices`
2. `btc_accept_pairing`
3. `tool_set_target_device_name PTS`
4. `hfp_init`
5. `tool_refresh_unique_id_using_bt_control`
6. [PTS Interaction] - Press OK if peer found
7. [PTS Interaction] - Press OK
8. `btc_connect`
9. [PTS Interaction] - Press OK
10. `btc_disconnect`

### HFP/AG/SDP/BV-01-I
ACTS Steps:
1. `btc_forget_all_known_devices`
2. `btc_accept_pairing`
4. `hfp_init`
6. [PTS Interaction] - Press OK
6. [PTS Interaction] - Press OK
7. [PTS Interaction] - Press OK (wait 30)
8. `hfp_incoming_call 12345678980`
9. [PTS Interaction] - Press OK
<TBD>

### HFP/AG/IIA/BV-01-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. `hfp_set_service_available true`
5. [PTS Interaction] - Press OK
6. `tool_refresh_unique_id_using_bt_control`
7. `btc_connect`
<TBD>

### HFP/AG/IIA/BV-02-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. `hfp_set_service_available true`
5. [PTS Interaction] - Press OK
6. `tool_refresh_unique_id_using_bt_control`
7. `btc_connect`
<TBD>

### HFP/AG/IIA/BV-05-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. `hfp_set_service_available true`
5. [PTS Interaction] - Press OK
6. `tool_refresh_unique_id_using_bt_control`
7. `btc_connect`
<TBD>


### HFP/AG/IID/BV-01-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. `hfp_set_service_available true`
5. [PTS Interaction] - Press OK
6. `tool_refresh_unique_id_using_bt_control`
7. `btc_connect`
<TBD>

### HFP/AG/IID/BV-03-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. `hfp_set_service_available true`
5. [PTS Interaction] - Press OK
6. `tool_refresh_unique_id_using_bt_control`
7. `btc_connect`
<TBD>

### HFP/AG/IID/BV-04-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. `hfp_set_service_available true`
5. [PTS Interaction] - Press OK
6. `tool_refresh_unique_id_using_bt_control`
7. `btc_connect`
<TBD>

### HFP/AG/IIC/BV-01-I
ACTS Steps:
1. `btc_accept_pairing`
2. `hfp_init`
3. `btc_forget_all_known_devices`
4. `tool_set_target_device_name PTS`
5. [PTS Interaction] - Press OK
6. `tool_refresh_unique_id_using_bt_control`
7. `btc_connect`
8. [PTS Interaction] - Press OK
9. `hfp_incoming_call 12345678980`
10. [PTS Interaction] - Press OK
11. [PTS Interaction] - Press OK
12. `hfp_list_calls` // Use latest call number from this list
13. `hfp_set_call_terminated #` // Number from previous ste
14. [PTS Interaction] - Press OK
15. `btc_disconnect`

### HFP/AG/IIC/BV-02-I
ACTS Steps:
1. `btc_accept_pairing`
2. `hfp_init`
3. `btc_forget_all_known_devices`
4. `tool_set_target_device_name PTS`
5. [PTS Interaction] - Press OK
6. `tool_refresh_unique_id_using_bt_control`
7. `btc_connect`
8. `hfp_set_battery_level 0`
9. [PTS Interaction] - Press OK
10. `hfp_set_service_available true`
11. [PTS Interaction] - Press OK
12. `hfp_set_service_available false`
13. [PTS Interaction] - Press OK
14. `btc_disconnect`

### HFP/AG/IIC/BV-03-I
ACTS Steps:
1. `btc_accept_pairing`
2. `hfp_init`
3. `btc_forget_all_known_devices`
4. `tool_set_target_device_name PTS`
5. [PTS Interaction] - Press OK
6. `tool_refresh_unique_id_using_bt_control`
7. `btc_connect`
8. [PTS Interaction] - Press OK
9. `hfp_set_service_available true`
10. [PTS Interaction] - Press OK
11. `hfp_set_service_available false`
12. `hfp_set_signal_strength 0`
13. [PTS Interaction] - Press OK
<TBD>

### HFP/AG/HFI/BV-02-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `btc_disconnect`

### HFP/AG/HFI/BI-03-I
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect`
7. [PTS Interaction] - Press OK
8. `btc_disconnect`

### IOPT/CL/HFP-AG/COD/BV-02-I
ACTS Steps:
1. `btc_accept_pairing`
2. `hfp_init`
3. `btc_forget_all_known_devices`
4. [PTS Interaction] - Press OK
5. `tool_set_target_device_name PTS`
6. [PTS Interaction] - Press OK
7. `tool_refresh_unique_id_using_bt_control`
8. `btc_connect`
9. [PTS Interaction] - Press OK
10. `btc_disconnect`
11. `btc_forget_all_known_devices`
12. [PTS Interaction] - Press OK
13. [PTS Interaction] - Press OK
14. `tool_set_target_device_name PTS`
15. `tool_refresh_unique_id_using_bt_control`
16. `btc_connect`
17. [PTS Interaction] - Press OK
18. `btc_disconnect`
19. `btc_forget_all_known_devices`
20. [PTS Interaction] - Press OK
21. [PTS Interaction] - Press OK
22. `tool_set_target_device_name PTS`
23. `tool_refresh_unique_id_using_bt_control`
24. `btc_connect`
25. [PTS Interaction] - Press OK
26. `btc_disconnect`
27. `btc_forget_all_known_devices`
28. [PTS Interaction] - Press OK
29. [PTS Interaction] - Press OK
30. `tool_set_target_device_name PTS`
31. `tool_refresh_unique_id_using_bt_control`
32. `btc_connect`
33. [PTS Interaction] - Press OK
34. `btc_disconnect`
35. `btc_forget_all_known_devices`
36. [PTS Interaction] - Press OK

### IOPT/CL/HFP-AG/SF/BV-13-I
ACTS Steps:
1. `btc_accept_pairing`
2. `hfp_init`
3. `btc_forget_all_known_devices`
4. [PTS Interaction] - Press OK
5. [PTS Interaction] - Press Yes
6. [PTS Interaction] - Press OK
7. `tool_refresh_unique_id_using_bt_control`
8. `btc_connect`
9. [PTS Interaction] - Press OK
10. `btc_disconnect`
