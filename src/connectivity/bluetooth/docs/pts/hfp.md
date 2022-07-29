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

## IUT Setup
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `rfcomm_init`

## TESTS

### HFP/AG/OOR/BV-02-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 12345678980`
8. [PTS Interaction] - Press OK

### HFP/AG/TRS/BV-01-C
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_set_service_available false`
8. [PTS Interaction] - Press OK
9. `hfp_set_service_available true`
10. [PTS Interaction] - Press OK
11. `btc_disconnect_device`

### HFP/AG/PSI/BV-01-C
1. Perform IUT setup
2. Launch PTS test
3. `hfp_set_signal_strength 5`
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect_device`
7. [PTS Interaction] - Press OK
8. `hfp_set_signal_strength 0`
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. `btc_disconnect_device`

### HFP/AG/PSI/BV-03-C
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. `hfp_set_battery_level 1`
7. [PTS Interaction] - Press OK
8. `hfp_set_battery_level 3`
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. `btc_disconnect_device`

### HFP/AG/PSI/BV-04-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. [PTS Interaction] - Press OK
8. `btc_disconnect_device`

### HFP/AG/PSI/BV-05-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. [PTS Interaction] - Press OK
5. `btc_disconnect_device`

### HFP/AG/ACS/BV-04-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 1234567`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. `hfp_get_state` - find call ID
11. `hfp_set_call_transferred_to_ag <call_ID>`	
12. [PTS Interaction] - Press OK
13. [PTS Interaction] - Press OK
14. `hfp_set_call_terminated <call_ID>`
15. [PTS Interaction] - Press OK
16. `btc_disconnect_device`

### HFP/AG/ACS/BV-08-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 1234567`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. `hfp_get_state` - find call ID
11. `hfp_set_call_transferred_to_ag <call_ID>`	
12. [PTS Interaction] - Press OK
13. [PTS Interaction] - Press OK
14. `hfp_set_call_terminated <call_ID>`
15. [PTS Interaction] - Press OK
16. `btc_disconnect_device`

### HFP/AG/ACS/BV-11-I
See HFP/AG/ACS/BV-08-I

### HFP/AG/ACS/BI-14-I

### HFP/AG/ACS/BV-16-I
See HFP/AG/ACS/BV-08-I

### HFP/AG/ACR/BV-01-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 1234567`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. `hfp_get_state` - find call ID
11. `hfp_set_call_terminated <call_ID>`
12. [PTS Interaction] - Press OK
13. `btc_disconnect_device`

### HFP/AG/ACR/BV-02-I
See HFP/AG/ACR/BV-01-I

### HFP/AG/CLI/BV-01-I
See HFP/AG/ACR/BV-01-I

### HFP/AG/ICA/BV-04-I
See HFP/AG/ACR/BV-01-I

### HFP/AG/ICA/BV-06-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 1234567`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. `hfp_get_state` - find call ID
11. `hfp_set_call_transferred_to_ag <call_ID>`
12. [PTS Interaction] - Press OK
13. `hfp_set_call_terminated <call_ID>`
14. [PTS Interaction] - Press OK
15. `btc_disconnect_device`

### HFP/AG/TCA/BV-01-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 1234567`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. `btc_disconnect_device`

### HFP/AG/TCA/BV-02-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 1234567`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. `hfp_get_state` - find call ID
11. `hfp_set_call_terminated <call_ID>`
12. [PTS Interaction] - Press OK
13. `btc_disconnect_device`

### HFP/AG/TCA/BV-03-I
See HFP/AG/TCA/BV-02-I

### HFP/AG/TCA/BV-04-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 1234567`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. `hfp_incoming_call 1234568`

### HFP/AG/TCA/BV-05-I

### HFP/AG/ATH/BV-03-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `hfp_incoming_call 1234567`
5. `hfp_get_state` - Find call ID
6. `hfp_set_call_active <call_ID>`
7. `hfp_set_call_terminated <call_ID`
8. `btc_disconnect_device`

### HFP/AG/ATH/BV-04-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 1234567`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. `hfp_get_state` - find call ID
11. `hfp_set_call_transferred_to_ag <call_ID>`	
12. [PTS Interaction] - Press OK
13. [PTS Interaction] - Press OK
14. [PTS Interaction] - Press OK
15. `hfp_set_call_terminated <call_ID>`
16. [PTS Interaction] - Press OK
17. `btc_disconnect_device`

### HFP/AG/ATH/BV-06-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 1234567`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. `hfp_get_state` - find call ID
11. `hfp_set_call_transferred_to_ag <call_ID>`	
12. [PTS Interaction] - Press OK
13. [PTS Interaction] - Press OK
14. `hfp_set_call_active <call_ID>`
15. [PTS Interaction] - Press OK
16. [PTS Interaction] - Press OK
17. `hfp_set_call_terminated <call_ID>`
18. [PTS Interaction] - Press OK
19. `btc_disconnect_device`

### HFP/AG/ATA/BV-01-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 1234567`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. `btc_disconnect_device`
11. [PTS Interaction] - Press OK
12. [PTS Interaction] - Press OK
13. `hfp_get_state` - find call ID
14. `hfp_set_call_terminated <call_ID>`
15. `btc_connect_device`
16. [PTS Interaction] - Press OK
17. `btc_disconnect_device`

### HFP/AG/ATA/BV-02-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 1234567`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
13. `hfp_get_state` - find call ID
14. `hfp_set_call_terminated <call_ID>`
15. [PTS Interaction] - Press OK
16. `btc_disconnect_device`

### HFP/AG/OCN/BV-01-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. `hfp_set_dial_result 1234567 0`
7. [PTS Interaction] - Press OK
8. [PTS Interaction] - Press OK
9. `hfp_get_state` - find call ID
10. `hfp_set_call_active <call_ID>`
11. [PTS Interaction] - Press OK
12. [PTS Interaction] - Press OK
13. `hfp_set_call_terminated <call_ID>`
14. [PTS Interaction] - Press OK
15. `btc_disconnect_device`

### HFP/AG/OCM/BV-01-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. `hfp_set_memory_location 1 1234567`
7. `hfp_set_dial_result 1234567 0`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. `hfp_get_state` - find call ID
11. `hfp_set_call_active <call_ID>`
12. [PTS Interaction] - Press OK
13. [PTS Interaction] - Press OK
14. `hfp_set_call_terminated <call_ID>`
15. [PTS Interaction] - Press OK
16. `btc_disconnect_device`


### HFP/AG/OCM/BV-02-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_clear_memory_location 1`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. [PTS Interaction] - Press OK
12. `btc_disconnect_device`

### HFP/AG/OCL/BV-01-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect_device`
4. [PTS Interaction] - Press OK
5. [PTS Interaction] - Press OK
6. `hfp_get_state` - find call ID
7. `hfp_set_call_active <call_ID>`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. `hfp_set_call_terminated <call_ID>`
12. [PTS Interaction] - Press OK
13. `btc_disconnect_device`

### HFP/AG/OCL/BV-02-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
7. `hfp_clear_last_dialed`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. `btc_disconnect_device`

### HFP/AG/TWC/BV-01-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 1234567`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. `hfp_waiting_call 7654321`
12. [PTS Interaction] - Press OK
13. [PTS Interaction] - Press OK
14. `hfp_get_state` - find active call ID
15. `hfp_set_call_terminated <call_ID>`
16. [PTS Interaction] - Press OK
17. `btc_disconnect_device`

### HFP/AG/TWC/BV-02-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 1234567`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. `hfp_incoming_call 7654321`

### HFP/AG/TWC/BV-03-I
See HFP/AG/TWC/BV-02-I

### HFP/AG/TWC/BV-05-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. [PTS Interaction] - Press OK
5. [PTS Interaction] - Press OK
6. `tool_refresh_unique_id_using_bt_control`
7. `btc_connect_device`
8. [PTS Interaction] - Press OK
9. `hfp_incoming_call 1234567`
10. `hfp_set_dial_result 7654321 0`
11. `hfp_set_last_dialed 7654321`
12. [PTS Interaction] - Press OK
13. `hfp_get_state` - find call ID of second call
14. `hfp_set_call_active <call_ID>`
15. [PTS Interaction] - Press OK
16. [PTS Interaction] - Press OK
17. [PTS Interaction] - Press OK
18. `hfp_get_state` - find call ID of first call
19. `hfp_set_call_terminated <call_ID>`
20. [PTS Interaction] - Press OK

### HFP/AG/CIT/BV-01-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 1234567`
8. [PTS Interaction] - Press OK
9. `hfp_get_state` - find call ID
10. `hfp_set_call_terminated <call_ID>`
11. [PTS Interaction] - Press OK
12. `btc_disconnect_device`

### HFP/AG/ENO/BV-01-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 1234567`
8. [PTS Interaction] - Press Ok
9. [PTS Interaction] - Press OK
10. [PTS Interaction] - Press OK
11. `hfp_get_state` - find call ID
12. `hfp_set_call_terminated <call_ID>`
13. [PTS Interaction] - Press OK
14. `btc_disconnect_device`

### HFP/AG/TDC/BV-01-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 1234567`
8. [PTS Interaction] - Press OK for all character prompts
9. [PTS Interaction] - Press OK
10. `hfp_get_state` - find call ID
11. `hfp_set_call_terminated <call_ID>`
12. [PTS Interaction] - Press OK
13. `btc_disconnect_device`

### HFP/AG/ECS/BV-01-I
1. Perform IUT setup
2. Launch PTS test
3. `hfp_incoming_call 1234567`
4. [PTS Interaction] - Press OK
5. `hfp_incoming_call 7654321`
6. `hfp_get_state` - find call ID of first and second calls
7. `hfp_set_call_held <first_call_ID>`
8. `hfp_set_call_active <second_call_ID>`

### HFP/AG/ECS/BV-02-I
### HFP/AG/ECS/BV-03-I
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. `tool_refresh_unique_id_using_bt_control`
5. `btc_connect_device`
6. [PTS Interaction] - Press OK
7. `hfp_incoming_call 1234567`
8. [PTS Interaction] - Press OK
9. [PTS Interaction] - Press OK
10. `hfp_incoming_call 7654321`

4. [PTS Interaction] - Press OK
5. `hfp_incoming_call 7654321`
6. `hfp_get_state` - find call ID of first and second calls
7. `hfp_set_call_held <first_call_ID>`
8. `hfp_set_call_active <second_call_ID>`

### HFP/AG/ECC/BI-03-I
1. Perform IUT setup
2. Launch PTS test
3. `hfp_incoming_call 7654321`
4. `hfp_incoming_call 1234567`
5. `hfp_get_state` - find call id of first and second calls
6. `hfp_set_call_active <first_call_id>`
7. `hfp_set_call_held <second_call_id>`
8. [PTS interaction] - press ok
9. [PTS interaction] - press ok
10. [PTS interaction] - press ok
11. [PTS interaction] - press ok

### HFP/AG/ECC/BI-04-I
1. Perform IUT setup
2. Launch PTS test
3. `hfp_incoming_call 7654321`
4. `hfp_incoming_call 1234567`
5. `hfp_get_state` - find call id of first and second calls
6. `hfp_set_call_active <first_call_id>`
7. `hfp_set_call_held <second_call_id>`
8. [PTS interaction] - press ok
9. [PTS interaction] - press ok
10. [PTS interaction] - press ok
11. [PTS interaction] - press ok

### HFP/AG/NUM/BV-01-I
1. Perform IUT setup
2. `hfp_remove_service`
3. Launch PTS test
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_connect_device`
7. [PTS Interaction] - Press OK
8. `btc_disconnect_device`

### HFP/AG/SLC/BV-01-C
1. Perform IUT setup
2. Launch PTS test
3. [PTS Interaction] - Press OK
4. [PTS Interaction] - Press OK
5. `tool_refresh_unique_id_using_bt_control`
6. `btc_disconnect_device`

### HFP/AG/SLC/BV-02-C
1. `hfp_init`
2. `tool_set_target_device_name PTS`
3. Launch PTS test
4. [PTS Interaction] - Press OK
5. [PTS Interaction] - Press OK
6. `tool_refresh_unique_id_using_bt_control`
7. `btc_connect_device`
8. [PTS Interaction] - Press OK
9. `btc_disconnect_device`

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
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `hfp_init`
4. [PTS Interaction] - Press OK
5. `hfp_incoming_call 1234567`
6. [PTS Interaction] - Press OK
7. [PTS Interaction] - Press OK
8. `hfp_get_state` - find call ID
9. `hfp_set_call_transferred_to_ag <call_ID>`
10. `hfp_set_call_active <call_ID>`
11. [PTS Interaction] - Press OK
12. [PTS Interaction] - Press OK
13. `hfp_set_call_terminated <call_ID>`
14. [PTS Interaction] - Press OK
15. `tool_refresh_unique_id_using_bt_control`
16. `btc_disconnect_device`

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
1. Perform IUT setup
2. Launch PTS test
3. `btc_forget_all_known_devices`
4. [PTS Interaction] - Press OK
5. `tool_set_target_device_name PTS`
6. `tool_refresh_unique_id_using_bt_control`
7. [PTS Interaction] - Press OK
8. `btc_connect_device`
9. [PTS Interaction] - Press OK
10. `btc_disconnect_device`
11. Repeat steps 3 to 11 four times

### IOPT/CL/HFP-AG/SF/BV-13-I
1. Perform IUT setup
2. Launch PTS test
3. `btc_forget_all_known_devices`
4. [PTS Interaction] - Press OK
5. [PTS Interaction] - Press Yes
6. [PTS Interaction] - Press OK
7. `tool_set_target_device_name PTS`
8. `tool_refresh_unique_id_using_bt_control`
9. `btc_connect_device`
10. [PTS Interaction] - Press OK
11. `btc_disconnect_device`
