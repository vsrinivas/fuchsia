# GAVDP PTS Instructions

## Setup
Tools used to pass GAVDP tests in PTS:
* ACTS (see \<fuchsia_root\>/src/connectivity/bluetooth/docs/pts/acts_pts_setup.md)
    * Use the BluetoothCmdLineTest tool for all tests:
        * act.py -c \<config\> -tc BluetoothCmdLineTest
* SL4F (see \<fuchsia_root\>/garnet/bin/sl4f/README.md)

## IXIT Values
No changes

## TESTS

### GAVDP/ACP/APP/CON/BV-01-C
ACTS Steps:
1. `avdtp_init sink`

### GAVDP/ACP/APP/TRC/BV-02-C
ACTS Steps:
1. `avdtp_init sink`
2. [PTS Interaction] - Verify PTS messages and press OK

### GAVDP/INT/APP/CON/BV-01-C
ACTS Steps:
1. `avdtp_init sink`
2. `tool_set_target_device_name PTS`
2. `tool_refresh_unique_id_using_bt_control`
3. `btc_connect`

### GAVDP/INT/APP/TRC/BV-02-C
TBD