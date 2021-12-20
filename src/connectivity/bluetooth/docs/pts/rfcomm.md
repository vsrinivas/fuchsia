# RFCOMM PTS Instructions

## Requirements
Tools used to pass RFCOMM tests in PTS:
* ACTS (see \<fuchsia_root\>/src/connectivity/bluetooth/docs/pts/acts_pts_setup.md)
    * Use the BluetoothCmdLineTest tool for all tests:
        * act.py -c \<config\> -tc BluetoothCmdLineTest
* SL4F (see \<fuchsia_root\>/src/testing/sl4f/README.md)
* bt-cli (see \<fuchsia_root\>/src/connectivity/bluetooth/tools/bt-cli)
* fx log

Notes:
* Run `fx log --only test-rfcomm-client` to enable logging in a separate terminal. Relevant RFCOMM
  values will be printed in this log.
* Ensure the HFP component is not running. If `ps | grep hfp` returns a process, terminate it by
  running `killall bt-hfp-audio-gateway.cm`.
* Some tests require ACTS commands which accept a channel number as an argument. This channel number can be discovered in the `fx log` output; it is usually the channel number specified in TSPX_server_channel_iut, but can sometimes differ

## IXIT Values
TSPX_bd_addr_iut = The address of your device

TSPX_server_channel_iut = The RFCOMM channel number that is advertised by the Fuchsia device. Use 1.

## IUT Setup
ACTS Steps:
1. `btc_accept_pairing`
2. `tool_set_target_device_name PTS`
3. `rfcomm_init`

## TESTS

### RFCOMM/DEVA/RFC/BV-01-C
1. Perform IUT setup
2. Launch PTS test
3. `btc_connect_device`
4. `rfcomm_connect_rfcomm_channel <channel_number>`

### RFCOMM/DEVB/RFC/BV-02-C
1. Perform IUT setup
2. Launch PTS test

### RFCOMM/DEVA-DEVB/RFC/BV-03-C
1. Perform IUT setup
2. Launch PTS test

### RFCOMM/DEVA-DEVB/RFC/BV-04-C
1. Perform IUT setup
2. Launch PTS test
3. `rfcomm_disconnect_session`

### RFCOMM/DEVA/RFC/BV-05-C
1. Perform IUT setup
2. Launch PTS test
3. `btc_connect_device`
4. `rfcomm_connect_rfcomm_channel <channel_number>`

### RFCOMM/DEVB/RFC/BV-06-C
1. Perform IUT setup
2. Launch PTS test

### RFCOMM/DEVA-DEVB/RFC/BV-07-C
1. Perform IUT setup
2. Launch PTS test
3. `rfcomm_disconnect_rfcomm_channel <channel_number>`

### RFCOMM/DEVA-DEVB/RFC/BV-08-C
1. Perform IUT setup
2. Launch PTS test

### RFCOMM/DEVA-DEVB/RFC/BV-11-C
1. Perform IUT setup
2. Launch PTS test

### RFCOMM/DEVA-DEVB/RFC/BV-13-C
1. Perform IUT setup
2. Launch PTS test

### RFCOMM/DEVA-DEVB/RFC/BV-15-C
1. Perform IUT setup
2. Launch PTS test
3. `btc_connect_device`
4. `rfcomm_connect_rfcomm_channel <channel_number>`

### RFCOMM/DEVA-DEVB/RFC/BV-17-C
1. Perform IUT setup
2. Launch PTS test

### RFCOMM/DEVA-DEVB/RFC/BV-19-C
1. Perform IUT setup
2. Launch PTS test

### RFCOMM/DEVA-DEVB/RFC/BV-21-C
1. Perform IUT setup
2. Launch PTS test
3. `rfcomm_write_rfcomm <channel_number> foobar123`

### RFCOMM/DEVA-DEVB/RFC/BV-22-C
1. Perform IUT setup
2. Launch PTS test
3. `rfcomm_write_rfcomm <channel_number> foobar123`

### RFCOMM/DEVA-DEVB/RFC/BV-25-C
1. Perform IUT setup
2. Launch PTS test
