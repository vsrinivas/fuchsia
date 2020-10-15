# SM PTS Instructions

## Setup
Tools used to pass SM tests in PTS:
* ACTS (see \<fuchsia_root\>/src/connectivity/bluetooth/docs/pts/acts_pts_setup.md)
* Use the BluetoothCmdLineTest tool for all tests:
* act.py -c \<config\> -tc BluetoothCmdLineTest
* SL4F (see \<fuchsia_root\>/src/testing/sl4f/README.md)

## IXIT Values
TBD

## TESTS
Note:
SM MAS Tests
* The LE address for PTS does not change. Each test is written with the assumption it is run for the first time. If `tool_set_target_device_name PTS` and `tool_refresh_unique_id` is run once, the unique peer ID will be saved (assuming the LE addressed was successfully found by device name).
* Setting the target device name (`tool_set_target_device_name PTS`) is not required if your ACTS config file contains this line:
    "target_device_name": "PTS"

### SM/MAS/PROT/BV-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_accept_pairing`
4. `gattc_connect`
5. `btc_pair ENCRYPTED BONDABLE LE`
6. `[PTS Interaction] Press ok and wait 30 seconds`

### SM/MAS/JW/BV-05-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_accept_pairing`
4. `gattc_connect`
5. `btc_pair ENCRYPTED BONDABLE LE`
6. `gattc_disconnect`
7. Repeat seps 4-6 x5 times

### SM/MAS/JW/BI-04-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_accept_pairing`
4. `gattc_connect`
5. `btc_pair ENCRYPTED BONDABLE LE`
6. `gattc_disconnect`

### SM/MAS/JW/BI-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_accept_pairing`
4. `gattc_connect`
5. `btc_pair ENCRYPTED BONDABLE LE`

### SM/MAS/PKE/BV-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_set_io_capabilities KEYBOARD DISPLAY`
4. `btc_accept_pairing`
5. `gattc_connect`
6. `btc_pair ENCRYPTED NON_BONDABLE LE`
7. `btc_get_pairing_pin`
8. [PTS Interaction] Input pairing pin to PTS
9. `gattc_disconnect`

### SM/MAS/PKE/BV-04-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_set_io_capabilities KEYBOARD DISPLAY`
4. `btc_accept_pairing`
5. `gattc_connect`
6. `btc_pair ENCRYPTED NON_BONDABLE LE`
7. `gattc_disconnect`

### SM/MAS/PKE/BI-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_set_io_capabilities KEYBOARD DISPLAY`
4. `btc_accept_pairing`
5. `gattc_connect`
6. `btc_pair ENCRYPTED NON_BONDABLE LE`
7. `btc_get_pairing_pin`
8. [PTS Interaction] Input pairing pin to PTS
9. `gattc_disconnect`

### SM/MAS/PKE/BI-02-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_set_io_capabilities KEYBOARD DISPLAY`
4. `btc_accept_pairing`
5. `gattc_connect`
6. `btc_pair ENCRYPTED NON_BONDABLE LE`
7. `btc_get_pairing_pin`
8. [PTS Interaction] Input pairing pin to PTS
9. [PTS Interaction] Press OK
10. `gattc_disconnect`

### SM/MAS/OOB/BV-05-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_set_io_capabilities KEYBOARD DISPLAY`
4. `btc_accept_pairing`
5. `gattc_connect`
6. `btc_pair ENCRYPTED NON_BONDABLE LE`
7. `btc_get_pairing_pin`
8. [PTS Interaction] Input pairing pin to PTS
9. `gattc_disconnect`

### SM/MAS/OOB/BV-07-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_accept_pairing`
4. `gattc_connect`
5. `btc_pair ENCRYPTED BONDABLE LE`
6. `gattc_disconnect`

### SM/MAS/EKS/BI-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_accept_pairing`
4. `gattc_connect`
5. `btc_pair ENCRYPTED BONDABLE LE`
6. `gattc_disconnect`

### SM/MAS/EKS/BV-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_accept_pairing`
4. `gattc_connect`
5. `btc_pair ENCRYPTED BONDABLE LE`
6. `gattc_disconnect`

### SM/MAS/KDU/BV-05-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_accept_pairing`
4. `gattc_connect`
5. `btc_pair ENCRYPTED BONDABLE LE`
6. `gattc_disconnect`

### SM/MAS/KDU/BV-06-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_set_io_capabilities KEYBOARD DISPLAY`
4. `btc_accept_pairing`
5. `gattc_connect`
6. `btc_pair ENCRYPTED BONDABLE LE`
7. `gattc_disconnect`

### SM/MAS/KDU/BV-10-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_set_io_capabilities KEYBOARD DISPLAY`
4. `btc_accept_pairing`
5. `gattc_connect`
6. `btc_pair ENCRYPTED BONDABLE LE`
7. [PTS Interaction] Verify value and press 'OK'
7. `gattc_disconnect`

### SM/MAS/KDU/BI-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_set_io_capabilities KEYBOARD DISPLAY`
4. `btc_accept_pairing`
5. `gattc_connect`
6. `btc_pair ENCRYPTED BONDABLE LE`
7. `gattc_disconnect`
8. [PTS Interaction] Press 'OK'
9. Repeat steps 5-8 x3 times

### SM/MAS/SIP/BV-02-C
NOTE: This test is flaky and will stall sometimes.
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_set_io_capabilities KEYBOARD DISPLAY`
4. `btc_accept_pairing`
5. `gattc_connect`
6. `gattc_disconnect`

### SM/MAS/SCJW/BV-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_set_io_capabilities KEYBOARD DISPLAY`
4. `btc_accept_pairing`
5. `gattc_connect`
6. `btc_pair ENCRYPTED NON_BONDABLE LE`
7. `gattc_disconnect`

### SM/MAS/SCJW/BV-04-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_set_io_capabilities KEYBOARD DISPLAY`
4. `btc_accept_pairing`
5. `gattc_connect`
6. `btc_pair ENCRYPTED NON_BONDABLE LE`
7. `gattc_disconnect`

### SM/MAS/SCJW/BI-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_set_io_capabilities KEYBOARD DISPLAY`
4. `btc_accept_pairing`
5. `gattc_connect`
6. `btc_pair ENCRYPTED NON_BONDABLE LE`
7. `gattc_disconnect`
8. [PTS Interaction] Press 'OK'
9. Repeat steps 5-8 x5 times
10. [PTS Interaction] Verify value at the end of the test and press OK

### SM/MAS/SCPK/BV-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_set_io_capabilities KEYBOARD NONE`
4. `btc_accept_pairing`
5. `gattc_connect`
6. `btc_pair ENCRYPTED NON_BONDABLE LE`
7. `btc_input_pairing_pin 000000`
8. [PTS Interaction] Input pairing pin 000000
9. `gattc_disconnect`

### SM/MAS/SCPK/BV-04-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_set_io_capabilities KEYBOARD NONE`
4. `btc_accept_pairing`
5. `gattc_connect`
6. `btc_pair ENCRYPTED NON_BONDABLE LE`
7. `btc_input_pairing_pin 000000`
8. [PTS Interaction] Input pairing pin 000000
9. `gattc_disconnect`

### SM/MAS/SCPK/BI-01-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_set_io_capabilities KEYBOARD NONE`
4. `btc_accept_pairing`
5. `gattc_connect`
6. `btc_pair ENCRYPTED NON_BONDABLE LE`
7. `gattc_disconnect`
8. [PTS Interaction] Press 'OK'
9. Repeat steps 5-8 x4 times
10. `gattc_connect`
11. `btc_pair ENCRYPTED NON_BONDABLE LE`
12. `btc_input_pairing_pin 000000`
13. [PTS Interaction] Input pairing pin 000000
14. `gattc_disconnect`

### SM/MAS/SCPK/BI-02-C
ACTS Steps:
1. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id`
3. `btc_set_io_capabilities KEYBOARD NONE`
4. `btc_accept_pairing`
5. `gattc_connect`
6. `btc_pair ENCRYPTED BONDABLE LE`
7. `btc_input_pairing_pin 000000`
8. [PTS Interaction] Input pairing pin 000000
9. `gattc_disconnect`

### SM/SLA/PROT/BV-02-C
ACTS Steps:
1. `btc_set_io_capabilities KEYBOARD DISPLAY`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`
4. Wait for 40 seconds

### SM/SLA/JW/BV-02-C
ACTS Steps:
1. `btc_set_io_capabilities KEYBOARD DISPLAY`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`

### SM/SLA/JW/BI-03-C
ACTS Steps:
1. `btc_set_io_capabilities KEYBOARD DISPLAY`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`

### SM/SLA/JW/BI-02-C
ACTS Steps:
1. `btc_set_io_capabilities KEYBOARD NONE`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`

### SM/SLA/PKE/BV-02-C
ACTS Steps:
1. `btc_set_io_capabilities KEYBOARD NONE`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`
4. `btc_input_pairing_pin xxxxxx` Where xxxxxx is the pin displayed by PTS

### SM/SLA/PKE/BV-05-C
ACTS Steps:
1. `btc_set_io_capabilities KEYBOARD DISPLAY`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`

### SM/SLA/PKE/BI-03-C
ACTS Steps:
1. `btc_set_io_capabilities KEYBOARD NONE`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`
4. `btc_input_pairing_pin 000000`
5. [PTS Interaction] - Input 000000 as the pairing pin when prompted

### SM/SLA/OOB/BV-06-C
ACTS Steps:
1. `btc_set_io_capabilities CONFIRMATION DISPLAY`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`
4. `btc_get_pairing_pin`
5. [PTS Interaction] - Input pairing pin from step 4 into PTS.

### SM/SLA/OOB/BV-08-C
ACTS Steps:
1. `btc_set_io_capabilities CONFIRMATION DISPLAY`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`

### SM/SLA/EKS/BV-02-C
ACTS Steps:
1. `btc_accept_pairing`
2. `ble_start_generic_connectable_advertisement`

### SM/SLA/EKS/BI-02-C
ACTS Steps:
1. `btc_set_io_capabilities DISPLAY KEYBOARD`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`

### SM/SLA/KDU/BV-01-C
ACTS Steps:
1. `btc_accept_pairing`
2. `ble_start_generic_connectable_advertisement`

### SM/SLA/KDU/BV-02-C
ACTS Steps:
1. `btc_accept_pairing`
2. `ble_start_generic_connectable_advertisement`

### SM/SLA/KDU/BV-07-C
ACTS Steps:
1. `btc_set_io_capabilities DISPLAY KEYBOARD`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`

### SM/SLA/KDU/BV-08-C
ACTS Steps:
1. `btc_accept_pairing`
2. `ble_start_generic_connectable_advertisement`


### SM/SLA/KDU/BI-01-C
ACTS Steps:
1. `btc_set_io_capabilities DISPLAY KEYBOARD`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`
4. `btc_forget_all_known_devices`
5. [PTS Interaction] - Press Yes
6. `ble_start_generic_connectable_advertisement`
7. `btc_forget_all_known_devices`
8. [PTS Interaction] - Press Yes
9. `ble_start_generic_connectable_advertisement`
10. `btc_forget_all_known_devices`
11. [PTS Interaction] - Press Yes
12. `ble_start_generic_connectable_advertisement`
13. `btc_forget_all_known_devices`
14. [PTS Interaction] - Press Yes


### SM/SLA/SIP/BV-01-C
TODO: ACTS needs to be able ot set the unique id instead of scanning for it. Use `bt-cli` in `fx shell` to send pairing request in step 5
ACTS Steps:
1. `btc_set_io_capabilities DISPLAY KEYBOARD`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`
4. `tool_set_unique_id <id>`
5. `btc_pair ENCRYPTED BONDABLE LE`

### SM/SLA/SIE/BV-01-C
Notes:
This test is flaky and needs some assisted steps to pass. Make sure to remove bonded devices on PTS and Fuchsia before the test.
ACTS needs to be able ot set the unique id instead of scanning for it. Use `bt-cli` in `fx shell` to send pairing request in step 6-7

ACTS Steps:
1. `btc_accept_pairing`
2. `ble_start_generic_connectable_advertisement`
3. `ble_start_generic_connectable_advertisement`
4. As soon as PTS connects to DUT, `fx shell > bt-cli > disconnect 00:1B:DC:F2:1E:0E` (Note this will not prompt you)
5. `ble_start_generic_connectable_advertisement`
6. `tool_set_unique_id <id>`
7. `btc_pair ENCRYPTED BONDABLE LE`

### SM/SLA/SCJW/BV-02-C
ACTS Steps:
1. `btc_accept_pairing`
2. `ble_start_generic_connectable_advertisement`

### SM/SLA/SCJW/BV-03-C
ACTS Steps:
1. `btc_accept_pairing`
2. `ble_start_generic_connectable_advertisement`

### SM/SLA/SCJW/BI-02-C
ACTS Steps:
1. `btc_accept_pairing`
2. `ble_start_generic_connectable_advertisement`

### SM/SLA/SCPK/BV-02-C
Note: Remove all bondings from PTS and Fuchsia before starting the test.

ACTS Steps:
1. `btc_set_io_capabilities DISPLAY KEYBOARD`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`
4. `btc_get_pairing_pin`
5. [PTS Interaction] - Verify pairing pin from step 4 matches PTS pairing pin and press OK

### SM/SLA/SCPK/BV-03-C
Note: Remove all bondings from PTS and Fuchsia before starting the test.
1. `btc_accept_pairing`
2. `ble_start_generic_connectable_advertisement`
3. `btc_input_pairing_pin 000000`
4. [PTS interaction] - Input 000000 as a pairing pin to PTS and press OK

### SM/SLA/SCPK/BI-03-C
Note: Remove all bondings from PTS and Fuchsia before starting the test.
ACTS Steps:
1. `btc_set_io_capabilities DISPLAY KEYBOARD`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`
4. `btc_get_pairing_pin`
5. [PTS Interaction] - Input pairing pin from step 4 into PTS and press OK


### SM/SLA/SCPK/BI-04-C
Note: Remove all bondings from PTS and Fuchsia before starting the test.
ACTS Steps:
1. `btc_set_io_capabilities KEYBOARD NONE`
2. `btc_accept_pairing`
3. `ble_start_generic_connectable_advertisement`
4. `ble_start_generic_connectable_advertisement`
5. `ble_start_generic_connectable_advertisement`
6. `ble_start_generic_connectable_advertisement`
7. `ble_start_generic_connectable_advertisement`
