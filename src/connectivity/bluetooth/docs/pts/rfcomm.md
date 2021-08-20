# RFCOMM PTS Instructions

## Setup
Tools used to pass RFCOMM tests in PTS:
* ACTS (see \<fuchsia_root\>/src/connectivity/bluetooth/docs/pts/acts_pts_setup.md)
    * Use the BluetoothCmdLineTest tool for all tests:
        * act.py -c \<config\> -tc BluetoothCmdLineTest
* SL4F (see \<fuchsia_root\>/src/testing/sl4f/README.md)
* bt-cli (see \<fuchsia_root\>/src/connectivity/bluetooth/tools/bt-cli)
* fx log

Notes:
* $PTS_ADDR is the integer address of the PTS Device. This can be found in bt-cli by doing:
  `bt-cli> list-devices`.
* Run `fx log --only test-rfcomm-client` to enable logging in a separate terminal. Relevant RFCOMM
  values will be printed in this log.
* Ensure the HFP component is not running. If `ps | grep hfp` returns a process, terminate it by
  running `killall bt-hfp-audio-gateway.cmx`.

## IXIT Values
TSPX_bd_addr_iut = The address of your device

TSPX_server_channel_uit = The RFCOMM channel number that is advertised by the Fuchsia device. Use 1.

## TESTS

### RFCOMM/DEVA/RFC/BV-01-C
1. In ACTS: `rfcomm_init`
2. In bt-cli: `connect $PTS_ADDR`
3. Look for the Server Channel number that is printed in the `fx log` output.
4. In ACTS: `rfcomm_connect_rfcomm_channel $PTS_ADDR $ServerChannelNumber`.

### RFCOMM/DEVB/RFC/BV-02-C
1. In ACTS: `rfcomm_init`

### RFCOMM/DEVA-DEVB/RFC/BV-03-C
1. In ACTS: `rfcomm_init`

### RFCOMM/DEVA-DEVB/RFC/BV-04-C
1. In ACTS: `rfcomm_init`
2. In ACTS: `rfcomm_disconnect_session $PTS_ADDR`.

### RFCOMM/DEVA/RFC/BV-05-C
1. In ACTS: `rfcomm_init`
2. In bt-cli: `connect $PTS_ADDR`
3. Look for the Server Channel number that is printed in the `fx log` output.
4. In ACTS: `rfcomm_connect_rfcomm_channel $PTS_ADDR $ServerChannelNumber`.

### RFCOMM/DEVB/RFC/BV-06-C
1. In ACTS: `rfcomm_init`

### RFCOMM/DEVA-DEVB/RFC/BV-07-C
1. In ACTS: `rfcomm_init`
2. Look for the Server Channel number that is printed in the `fx log` output.
3. In ACTS: `rfcomm_disconnect_rfcomm_channel $PTS_ADDR $ServerChannelNumber`.

### RFCOMM/DEVA-DEVB/RFC/BV-08-C
1. In ACTS: `rfcomm_init`

### RFCOMM/DEVA-DEVB/RFC/BV-11-C
1. In ACTS: `rfcomm_init`

### RFCOMM/DEVA-DEVB/RFC/BV-13-C
1. In ACTS: `rfcomm_init`

### RFCOMM/DEVA-DEVB/RFC/BV-14-C
1. In ACTS: `rfcomm_init`
2. Look for the Server Channel number that is printed in the `fx log` output.
3. In ACTS: `rfcomm_send_remote_line_status $PTS_ADDR $ServerChannelNumber`.

### RFCOMM/DEVA-DEVB/RFC/BV-15-C
1. In ACTS: `rfcomm_init`

### RFCOMM/DEVA-DEVB/RFC/BV-17-C
1. In ACTS: `rfcomm_init`

### RFCOMM/DEVA-DEVB/RFC/BV-19-C
1. In ACTS: `rfcomm_init`

### RFCOMM/DEVA-DEVB/RFC/BV-21-C
1. In ACTS: `rfcomm_init`
2. Look for the Server Channel number that is printed in the `fx log` output.
3. In ACTS: `rfcomm_write_rfcomm $PTS_ADDR $ServerChannelNumber foobar123`.
4. Wait 1-2 seconds.
5. In ACTS: `rfcomm_write_rfcomm $PTS_ADDR $ServerChannelNumber foobar123`.

### RFCOMM/DEVA-DEVB/RFC/BV-22-C
1. In ACTS: `rfcomm_init`
2. Look for the Server Channel number that is printed in the `fx log` output.
3. In ACTS: `rfcomm_write_rfcomm $PTS_ADDR $ServerChannelNumber foobar123`.
4. Wait 1-2 seconds.
5. In ACTS: `rfcomm_write_rfcomm $PTS_ADDR $ServerChannelNumber foobar123`.

### RFCOMM/DEVA-DEVB/RFC/BV-25-C
1. In ACTS: `rfcomm_init`
