# L2CAP PTS Instructions

## Setup
Tools used to pass L2CAP tests in PTS:
* ACTS (see \<fuchsia_root\>/src/connectivity/bluetooth/docs/pts/acts_pts_setup.md)
    * Use the BluetoothCmdLineTest tool for all tests:
        * act.py -c \<config\> -tc BluetoothCmdLineTest
* SL4F (see \<fuchsia_root\>/garnet/bin/sl4f/README.md)

## IXIT Values

## TESTS


### L2CAP/COS/CED/BV-01-C
Note: Set TSPX_psm to 0001
ACTS Steps:
1. `sdp_init`
2. `sdp_pts_example_1`
3. `tool_refresh_unique_id_using_bt_control`
4. `btc_connect`
5. `sdp_cleanup`
6. `btc_disconnect`

### L2CAP/COS/CED/BV-03-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627
profile> channels
profile> write <channel> a

### L2CAP/COS/CED/BV-04-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627
profile> channels
profile> disconnect <all channels from previous command>

### L2CAP/COS/CED/BV-05-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627

### L2CAP/COS/CED/BV-07-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627

### L2CAP/COS/CED/BV-08-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627

### L2CAP/COS/CED/BV-09-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627
profile> channels
profile> disconnect <all channels from previous command>

### L2CAP/COS/CED/BV-11-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627

### L2CAP/COS/CED/BI-01-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627

### L2CAP/COS/CFD/BV-01-C
ACTS Steps:
1. `btc_set_discoverable true`

### L2CAP/COS/CFD/BV-02-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627

### L2CAP/COS/CFD/BV-03-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627

### L2CAP/COS/CFD/BV-08-C
Note: Set TSPX_psm to 0001
ACTS Steps:
1. `avdtp_init sink`
2. `tool_refresh_unique_id_using_bt_control`
3. `btc_connect`
4. `btc_disconnect`

### L2CAP/COS/CFD/BV-09-C
Note: Set TSPX_psm to 0001
Run pairing delegate
connect to psm 0001 and write 48 bytes of data

### L2CAP/COS/CFD/BV-11-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627

### L2CAP/COS/CFD/BV-12-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627

### L2CAP/COS/IEX/BV-02-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627

### L2CAP/COS/ECH/BV-01-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627

### L2CAP/CMC/BV-01-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627

### L2CAP/CMC/BV-02-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627

### L2CAP/CMC/BV-03-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627

### L2CAP/CMC/BV-07-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627

### L2CAP/CMC/BV-10-CNote: Set TSPX_psm to 0001
ACTS Steps:
1. `avdtp_init sink`
2. `tool_refresh_unique_id_using_bt_control`
3. `btc_connect`
Note: Wait for the second disconnect dialog before sending disconnection command
4. `btc_disconnect`


### L2CAP/ERM/BV-01-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024
profile> write <channel> a
profile> write <channel> a
profile> write <channel> a

### L2CAP/ERM/BV-02-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024

### L2CAP/ERM/BV-03-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024

### L2CAP/ERM/BV-05-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024
profile> write <channel> a
profile> write <channel> a

### L2CAP/ERM/BV-06-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024
profile> write <channel> a
profile> write <channel> a

### L2CAP/ERM/BV-08-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024
profile> write <channel> a

### L2CAP/ERM/BV-09-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024
[PTS Interaction] - Press OK

### L2CAP/ERM/BV-10-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024
profile> write <channel> a
profile> write <channel> a

### L2CAP/ERM/BV-11-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024
profile> write <channel> a
[Wait for the timeout]

### L2CAP/ERM/BV-12-C

### L2CAP/ERM/BV-13-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024
profile> write <channel> a
profile> write <channel> a


### L2CAP/ERM/BV-14-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024
profile> write <channel> a
profile> write <channel> a
profile> write <channel> a
profile> write <channel> a

### L2CAP/ERM/BV-15-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024
profile> write <channel> a
profile> write <channel> a
profile> write <channel> a
profile> write <channel> a

### L2CAP/ERM/BV-18-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024
profile> write <channel> a

### L2CAP/ERM/BV-19-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024
profile> write <channel> a

### L2CAP/ERM/BV-20-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024
profile> write <channel> a

### L2CAP/ERM/BI-03-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024
profile> write <channel> a
profile> write <channel> a

### L2CAP/ERM/BI-04-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024
profile> write <channel> a
profile> write <channel> a

### L2CAP/ERM/BI-05-C
Note: Set TSPX_psm to 0A5F
bt-bredr-profile Steps:
profile> advertise 2655 ertm 1024
profile> write <channel> a
profile> write <channel> a

### L2CAP/EXF/BV-01-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627

### L2CAP/EXF/BV-05-C
Note: Set TSPX_psm to 0019
bt-bredr-profile Steps:
profile> advertise 25 e 627

### L2CAP/FIX/BV-01-C
Note: Set TSPX_psm to 0001
ACTS Steps:
1. `tool_refresh_unique_id_using_bt_control`
2. `btc_connect`

### L2CAP/LE/CPU/BI-01-C
### L2CAP/LE/CPU/BI-02-C
ACTS Steps:
1. `ble_start_generic_connectable_advertisement`

### L2CAP/LE/CPU/BV-01-C
Note: Set TSPX_iut_address_type_random to True
Note: Put in the LE address to TSPX_bd_addr_iut_le
ACTS Steps:
1. `ble_start_generic_connectable_advertisement`
2. [wait 10 seconds]

### L2CAP/LE/CPU/BV-02-C
### L2CAP/LE/REJ/BI-01-C
Note: Set TSPX_iut_address_type_random to True
Note: Put in the LE address to TSPX_bd_addr_iut_le
ACTS Steps:
1. `ble_start_generic_connectable_advertisement`