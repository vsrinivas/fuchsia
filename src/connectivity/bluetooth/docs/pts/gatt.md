# GATT PTS Instructions

## Setup
Tools used to pass GATT tests in PTS:
* ACTS (see \<fuchsia_root\>/src/connectivity/bluetooth/docs/pts/acts_pts_setup.md)
* Use the BluetoothCmdLineTest tool for all tests:
* act.py -c \<config\> -tc BluetoothCmdLineTest
* SL4F (see \<fuchsia_root\>/garnet/bin/sl4f/README.md)

## IXIT Values
TBD

## TESTS
Note:
GATT Client Tests
* The LE address for PTS does not change. Each test is written with the assumption it is run for the first time. If `tool_set_target_device_name PTS` and `tool_refresh_unique_id` is run once, the unique peer ID will be saved (assuming the LE addressed was successfully found by device name).
* Setting the target device name (`tool_set_target_device_name PTS`) is not required if your ACTS config file contains this line:
    "target_device_name": "PTS"

### GATT/CL/GAC/BV-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_write_long_char_by_id [input handle] 0 [input size]`

### GATT/CL/GAD/BV-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_list_services`
5. [PTS Interaction] - Verify services discovered match expected and press OK
6. `gattc_disconnect`
7. Repeat steps 3-6 three times.

### GATT/CL/GAD/BV-02-C
NOTE: UUIDs subject to change and command `gattc_set_discovery_uuid` subject to change

ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_set_discovery_uuid 1800`
4. `gattc_connect`
5. [PTS Interaction] - Verify value and press OK
6. `gattc_disconnect`
7. `gattc_set_discovery_uuid 1801`
8. `gattc_connect`
9. [PTS Interaction] - Verify value and press OK
10. `gattc_disconnect`
11. `gattc_set_discovery_uuid 33a7`
12. `gattc_connect`
13. [PTS Interaction] - Verify value and press OK
14. `gattc_disconnect`
15. `gattc_set_discovery_uuid a00b`
16. `gattc_connect`
17. [PTS Interaction] - Verify value and press OK
18. `gattc_disconnect`
19. `gattc_set_discovery_uuid 6634`
20. `gattc_connect`
21. [PTS Interaction] - Verify value and press OK
22. `gattc_disconnect`
23. `gattc_set_discovery_uuid 0549`
24. `gattc_connect`
25. [PTS Interaction] - Verify value and press OK
26. `gattc_disconnect`
27. `gattc_set_discovery_uuid 1137`
28. `gattc_connect`
29. [PTS Interaction] - Verify value and press OK
30. `gattc_disconnect`
31. `gattc_connect`
32. [PTS Interaction] - Verify value and press OK
33. `gattc_disconnect`

### GATT/CL/GAD/BV-03-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_list_services true`
5. [PTS Interaction] - Verify included services discovered and press OK
6. `gattc_disconnect`
7. Repeat steps 3-6 three times.
 
### GATT/CL/GAD/BV-04-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_list_services true`
5. [PTS Interaction] - Verify included services discovered and press OK
6. `gattc_disconnect`
7. Repeat steps 3-6 three times.

### GATT/CL/GAD/BV-05-C
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_list_services true`
5. [PTS Interaction] - Verify included services discovered and press OK
6. `gattc_disconnect`
7. Repeat steps 3-6 eight times.

### GATT/CL/GAD/BV-06-C
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_list_services true`
5. [PTS Interaction] - Verify included services discovered and press OK
6. `gattc_disconnect`
7. Repeat steps 3-6 four times.

### GATT/CL/GAR/BV-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_read_char_by_id [PTS specified handle]`
5.  [PTS Interaction] Verify Invalid handle error and press OK
6. `gattc_disconnect`

### GATT/CL/GAR/BI-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_read_char_by_id [PTS specified handle]`
5.  [PTS Interaction] Verify Read not permitted error and press OK
6. `gattc_disconnect`

### GATT/CL/GAR/BI-02-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_read_char_by_id [PTS specified handle]`
5.  [PTS Interaction] Verify Characteristic data read matches and press OK
6. `gattc_disconnect`

### GATT/CL/GAR/BI-04-C

### GATT/CL/GAR/BI-05-C
ACTS Steps:
Pre-condition: Verify there are no bonded devices on Fuchsia `btc_forget_all_known_devices` and PTS.

1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_accept_pairing`
4. `gattc_connect`
5. `gattc_read_char_by_id [PTS specified handle]`
6.  [PTS Interaction] Verify Insufficient Authentication error matches and press OK
7. `gattc_disconnect`

### GATT/CL/GAR/BV-03-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_read_char_by_uuid [PTS specified uuid]`
5. [PTS Interaction] Verify Value read matchs PTS and press OK
6. `gattc_read_char_by_uuid 0000b009-0000-0000-0123-456789abcdef`
7. [PTS Interaction] Verify Value read matchs PTS and press OK
8. `gattc_disconnect`

### GATT/CL/GAR/BI-06-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_read_char_by_uuid [PTS specified uuid]`
5. [PTS Interaction] Verify Value read matchs PTS and press OK
6. `gattc_disconnect`

### GATT/CL/GAR/BI-07-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_read_char_by_uuid [PTS specified uuid]`
5. [PTS Interaction] Verify Value read matchs PTS and press OK
6. `gattc_disconnect`


### GATT/CL/GAR/BI-10-C
ACTS Steps:
Pre-condition: Verify there are no bonded devices on Fuchsia `btc_forget_all_known_devices` and PTS.

1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_accept_pairing`
4. `gattc_connect`
5. `gattc_read_char_by_uuid [PTS specified uuid]`
6.  [PTS Interaction] Verify Insufficient Authentication error matches and press OK
7. `gattc_disconnect`

### GATT/CL/GAR/BI-11-C
ACTS Steps:
Pre-condition: Verify there are no bonded devices on Fuchsia `btc_forget_all_known_devices` and PTS.

1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_accept_pairing`
4. `gattc_connect`
5. `gattc_read_char_by_uuid [PTS specified uuid]`
6.  [PTS Interaction] Verify Insufficient Encryption key size error matches and press OK
7. `gattc_disconnect`

### GATT/CL/GAR/BV-04-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_read_long_char_by_id [PTS specific handle]] 0 10`
5. [PTS Interaction] Verify Value read matchs PTS and press OK
6. `gattc_read_long_char_by_id [PTS specific handle]] 0 10`
7. [PTS Interaction] Verify Value read matchs PTS and press OK
8. `gattc_read_long_char_by_id [PTS specific handle]] 0 10`
9. [PTS Interaction] Verify Value read matchs PTS and press OK
10. `gattc_read_char_by_id [PTS specific handle]]`
11. [PTS Interaction] Verify Value read matchs PTS and press OK
12. `gattc_disconnect`

### GATT/CL/GAR/BI-12-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_read_char_by_id [id]`
5. [PTS Interaction] - Verify Read Not Permitted returned and press OK
6. `gattc_disconnect`

### GATT/CL/GAR/BI-13-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_read_long_char_by_id [id] 100 10`
5. [PTS Interaction] - Verify Invalid Offset returned and press OK
6. `gattc_disconnect`

### GATT/CL/GAR/BI-14-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_read_char_by_id [id]`
5. [PTS Interaction] - Verify Invalid Handle returned and press OK
6. `gattc_disconnect`

### GATT/CL/GAR/BI-16-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_set_io_capability KEYBOARD DISPLAY`
4. `btc_accept_pairing`
5. `gattc_connect`
6. `gattc_read_char_by_id [id]`
7. [PTS Interaction] - Verify Insufficient Authentication error and press OK
8. `gattc_disconnect`

### GATT/CL/GAR/BI-17-C
ACTS Steps:
Pre-condition: Verify there are no bonded devices on Fuchsia `btc_forget_all_known_devices` and PTS.

1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_accept_pairing`
4. `gattc_connect`
5. `gattc_read_char_by_id [id]`
6. [PTS Interaction] - Verify Insufficient Encryption Key returned and press OK
7. `gattc_disconnect`

### GATT/CL/GAR/BV-06-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_read_desc_by_id [id]`
5. [PTS Interaction] - Verify Descriptor read value and press OK
6. `gattc_disconnect`

### GATT/CL/GAR/BV-07-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_read_desc_by_id [id]
5. [PTS Interaction] - Verify Descriptor read value and press OK
6. `gattc_read_desc_by_id [id]
7. [PTS Interaction] - Verify Descriptor read value and press OK
8. `gattc_read_desc_by_id [id]
9. [PTS Interaction] - Verify Descriptor read value and press OK
10. `gattc_read_desc_by_id [id]
11. [PTS Interaction] - Verify Descriptor read value and press OK
12. `gattc_disconnect`

### GATT/CL/GAR/BI-35-C
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_read_desc_by_id [id]`
5. [PTS Interaction] - Verify Application Error and press OK
6. `gattc_disconnect`




### GATT/CL/GAR/BV-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_read_char_by_id [PTS specified handle]`
5.  [PTS Interaction] - Verify Characteristic data read matches and press OK
6. `gattc_disconnect`

### GATT/CL/GAR/BI-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_read_char_by_id [PTS specified handle]`
5.  [PTS Interaction] - Verify Invalid handle error received and press OK
6. `gattc_disconnect`

### GATT/CL/GAR/BI-02-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_read_char_by_id [PTS specified handle]`
5.  [PTS Interaction] - Verify Read not permitted error received and press OK
6. `gattc_disconnect`

### GATT/CL/GAR/BI-04-C
Pre-steps:
Verify PTS and Fuchsia have deleted their link keys. With ACTS: `btc_forget_all_known_devices`
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_accept_pairing`
4. `gattc_connect`
5. `gattc_read_char_by_id [PTS specified handle]`
5.  [PTS Interaction] - Verify encryption key size error received and press OK
6. `gattc_disconnect`


### GATT/CL/GAW/BV-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_write_char_by_id_without_response [id] [size]`
5. `gattc_disconnect`

### GATT/CL/GAW/BV-03-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_write_char_by_id [id] 0 [size]`
5. `gattc_disconnect`

### GATT/CL/GAW/BI-02-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_write_char_by_id [id] 0 [size]`
5. [PTS Interaction] - Verify Invalid handle response returned and press OK
6. `gattc_disconnect`

### GATT/CL/GAW/BI-03-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_write_char_by_id [id] 0 [size]`
5. [PTS Interaction] - Verify Write not permitted response returned and press OK
6. `gattc_disconnect`

### GATT/CL/GAW/BI-05-C


### GATT/CL/GAW/BI-06-C
ACTS Steps:
Pre-steps:
Verify PTS and Fuchsia have deleted their link keys. With ACTS: `btc_forget_all_known_devices`

1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_accept_pairing`
4. `gattc_connect`
5. `gattc_write_char_by_id [id] 0 [size]`
6. [PTS Interaction] - Verify insufficient encryption key size error returned and press OK
7. `gattc_disconnect`

### GATT/CL/GAW/BV-05-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_write_long_char_by_id [id] 0 [size]`
5. `gattc_disconnect`

### GATT/CL/GAW/BI-07-C
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_write_long_char_by_id [id] 0 [size]`
5. [PTS Interaction] - Verify Invalid Handle response returned and press OK
6. `gattc_disconnect`

### GATT/CL/GAW/BI-08-C
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_write_long_char_by_id [id] 0 [size]`
5. [PTS Interaction] - Verify Write not permitted response returned and press OK
6. `gattc_disconnect`

### GATT/CL/GAW/BI-09-C
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_write_long_char_by_id [id] [offset size > PTS provided values] 1`
5. [PTS Interaction] - Verify Invalid offset response returned and press OK
6. `gattc_disconnect`

### GATT/CL/GAW/BI-12-C

### GATT/CL/GAW/BI-13-C
ACTS Steps:
Pre-steps:
Verify PTS and Fuchsia have deleted their link keys. With ACTS: `btc_forget_all_known_devices`

1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_accept_pairing`
4. `gattc_connect`
5. `gattc_write_long_char_by_id [id] 0 [size]`
6. [PTS Interaction] - Verify insufficient encryption key size error returned and press OK
7. `gattc_disconnect`

### GATT/CL/GAW/BV-06-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_write_long_char_by_id [id] 0 [size]`
5. `gattc_disconnect`

### GATT/CL/GAW/BV-08-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_write_desc_by_id [id] 0 [size]`
5. `gattc_disconnect`

### GATT/CL/GAW/BV-09-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_write_long_desc_by_id [id] 0 [size]`
5. `gattc_disconnect`

### GATT/CL/GAW/BI-32-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_write_long_char_by_id [id] 0 [size] true`
5. `gattc_disconnect`

### GATT/CL/GAW/BI-33-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_write_char_by_id [id] 0 [size]`
5. [PTS Interaction] - Verify invalid attribute length error returned and press OK
6. `gattc_disconnect`

### GATT/CL/GAW/BI-34-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_write_long_char_by_id [id] 0 [size]`
5. [PTS Interaction] - Verify invalid attribute length error returned and press OK
6. `gattc_disconnect`

### GATT/CL/GAN/BV-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_notify_all_chars`
5. [PTS Interaction] - Verify Characteristic notified and press OK
6. `gattc_disconnect`

### GATT/CL/GAI/BV-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_notify_all_chars`
5. `gattc_disconnect`

### GATT/CL/GAS/BV-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_notify_all_chars`
5. `gattc_disconnect`

### GATT/CL/GAT/BV-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_read_char_by_id [id]`
[Wait 30 seconds for timeout]

### GATT/CL/GAT/BV-02-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `gattc_connect`
4. `gattc_write_char_by_id [id] 0 [size]`
[Wait 30 seconds for timeout]

### GATT/SR/GAC/BI-01-C

### GATT/SR/GAC/BV-01-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`
3. `btc_forget_all_known_devices`
4. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAD/BV-01-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Verify services and press OK

### GATT/SR/GAD/BV-02-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Verify services and press OK

### GATT/SR/GAD/BV-03-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Verify services and press OK

### GATT/SR/GAD/BV-04-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAD/BV-05-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAD/BV-06-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAR/BV-01-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAR/BI-01-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAR/BI-02-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] Input ffff

### GATT/SR/GAR/BI-04-C
Note:
* Make sure no pairing delegates are running before running the test
ACTS Steps:
1. `gatts_setup_database LARGE_DB_3`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAR/BI-05-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_3`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAR/BV-03-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAR/BI-06-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] Input b007
3. [PTS Interaction] Input 0022

### GATT/SR/GAR/BI-07-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_3`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] Enter 9999

### GATT/SR/GAR/BI-08-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAR/BI-10-C
Note:
* Make sure no pairing delegates are running before running the test

Pre-condition: Verify there are no bonded devices on Fuchsia `btc_forget_all_known_devices` and PTS.
ACTS Steps:
1. [PTS Interaction] - Make sure IUT does not initiate pairing procedure and press OK
2. `gatts_setup_database LARGE_DB_3`
3. `ble_start_generic_connectable_advertisement`
4. [PTS Interaction] Press OK
5. [PTS Interaction] Enter b006
6. [PTS Interaction] Enter 0074

### GATT/SR/GAR/BI-11-C
Note:
* Make sure no pairing delegates are running before running the test

Pre-condition: Verify there are no bonded devices on Fuchsia `btc_forget_all_known_devices` and PTS.
ACTS Steps:
1. `gatts_setup_database LARGE_DB_3`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] Press OK
4. [PTS Interaction] Enter b006
5. [PTS Interaction] Enter 0074


### GATT/SR/GAR/BV-04-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Verify Read value and press OK


### GATT/SR/GAR/BI-12-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Enter 0018

### GATT/SR/GAR/BI-13-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_3`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAR/BI-14-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_3`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Enter ffff

### GATT/SR/GAR/BI-16-C
Note:
* Make sure no pairing delegates are running before running the test

Pre-condition: Verify there are no bonded devices on Fuchsia `btc_forget_all_known_devices` and PTS.
ACTS Steps:
1. [PTS Interaction] - Make sure IUT does not initiate pairing procedure and press OK
2. `gatts_setup_database LARGE_DB_3`
3. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAR/BI-17-C
* Make sure no pairing delegates are running before running the test

Pre-condition: Verify there are no bonded devices on Fuchsia `btc_forget_all_known_devices` and PTS.
ACTS Steps:
1. `gatts_setup_database LARGE_DB_3`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAR/BV-06-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_3`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Verify descriptor read value and press OK


### GATT/SR/GAR/BV-07-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Verify descriptor read value and press OK

### GATT/SR/GAR/BV-08-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Verify descriptor read value and press OK


### GATT/SR/GAW/BV-01-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Verify write value press OK


### GATT/SR/GAW/BV-03-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Verify write value press OK

### GATT/SR/GAW/BI-02-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Enter ffff

### GATT/SR/GAW/BI-03-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAW/BI-05-C
Pre-condition: Verify there are no bonded devices on Fuchsia `btc_forget_all_known_devices` and PTS.
ACTS Steps:
1. [PTS Interaction] - Make sure IUT does not initiate pairing procedure and press OK
2. `btc_accept_pairing`
3. `gatts_setup_database LARGE_DB_3`
4. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAW/BI-06-C
ACTS Steps:
Pre-condition: Verify there are no bonded devices on Fuchsia `btc_forget_all_known_devices` and PTS.

1. `gatts_setup_database LARGE_DB_3`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAW/BV-05-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_3`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAW/BI-07-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_3`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Input ffff

### GATT/SR/GAW/BI-08-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Enter 0009
2a01, 0009

### GATT/SR/GAW/BI-09-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_1`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAW/BI-12-C
Pre-condition: Verify there are no bonded devices on Fuchsia `btc_forget_all_known_devices` and PTS.
ACTS Steps:
1. [PTS Interaction] - Make sure IUT does not initiate pairing procedure and press OK
2. `btc_accept_pairing`
3. `gatts_setup_database LARGE_DB_3`
4. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAW/BI-13-C
Pre-condition: Verify there are no bonded devices on Fuchsia `btc_forget_all_known_devices` and PTS.
ACTS Steps:
1. [PTS Interaction] - Make sure IUT does not initiate pairing procedure and press OK
2. `btc_accept_pairing`
3. `gatts_setup_database LARGE_DB_3`
4. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAW/BV-06-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_3`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAW/BV-10-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_3`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAW/BV-11-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_3`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAW/BV-07-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_3`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAW/BV-08-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_3`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAW/BV-09-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_3`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAW/BI-32-C
ACTS Steps:
1. `gatts_setup_database DB_TEST`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAW/BI-33-C
ACTS Steps:
1. `gatts_setup_database LARGE_DB_3`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/GAN/BV-01-C
bt-le-heart-rate-peripheral

### GATT/SR/GAI/BV-01-C
bt-le-heart-rate-peripheral

### GATT/SR/GAS/BV-01-C
Note: Steps still in flux
ACTS Steps:
1. `btc_accept_pairing`
2. `ble_start_generic_connectable_advertisement`
3. [PTS Interaction] - Press ok and run step 4 after disconnect.
4. fx shell bt-le-battery-service (TODO: Replace this with an ACTS DB)
5. `ble_start_generic_connectable_advertisement`
6. [PTS Interaction] - Press ok
7. After PTS reconnects, run `gatts_setup TEST_DB_1`

### GATT/SR/GAT/BV-01-C
bt-le-heart-rate-peripheral
+
ACTS Steps after PTS prompt:
1. `gatts_setup_database DB_TEST`

### GATT/SR/UNS/BI-01-C
ACTS Steps:
1. `gatts_setup_database DB_TEST`
2. `ble_start_generic_connectable_advertisement`

### GATT/SR/UNS/BI-02-C
ACTS Steps:
1. `gatts_setup_database DB_TEST`
2. `ble_start_generic_connectable_advertisement`


### GATT/SR/UNS/BI-01-C
### GATT/SR/UNS/BI-02-C

