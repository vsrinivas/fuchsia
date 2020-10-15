# AVDTP PTS Instructions

## Setup
Tools used to pass AVDTP tests in PTS:
* ACTS (see \<fuchsia_root\>/src/connectivity/bluetooth/docs/pts/acts_pts_setup.md)
    * Use the BluetoothCmdLineTest tool for all tests:
        * act.py -c \<config\> -tc BluetoothCmdLineTest
* SL4F (see \<fuchsia_root\>/src/testing/sl4f/README.md)

## IXIT Values
TSPX_security_enabled = True

## TESTS

### AVDTP/SNK/ACP/L2C/BM/BV-01-C
NOT IN PTS

### AVDTP/SNK/ACP/L2C/EM/BV-02-C
NOT IN PTS

### AVDTP/SNK/ACP/SIG/SMG/BI-05-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BI-08-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BI-11-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BI-14-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BI-17-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BI-20-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BI-23-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BI-26-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BI-33-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BV-06-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BV-08-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BV-10-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BV-12-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BV-14-C
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BV-16-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BV-18-C
ACTS Steps:
1. `avdtp_init sink`
2. [PTS Interaction] - Confirm Audio heard through DUT

### AVDTP/SNK/ACP/SIG/SMG/BV-20-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BV-22-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BV-24-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/BV-26-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SRC/ACP/SIG/SMG/BI-05-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/BI-08-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/BI-11-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/BI-14-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/BI-17-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/BI-20-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/BI-23-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/BI-26-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/BI-33-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/ESR04/BI-28-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SNK/ACP/SIG/SMG/ESR04/BI-28-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/SIG/SMG/ESR05/BI-14-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/ACP/TRA/BTR/BV-02-C
ACTS Steps:
1. `avdtp_init sink`
2. [PTS Interaction] - Confirm Audio heard through DUT

### AVDTP/SNK/ACP/TRA/BTR/BI-01-C
ACTS Steps:
1. `avdtp_init sink`
2. [PTS Interaction] - Confirm Audio heard through DUT

### AVDTP/SNK/INT/L2C/BM/BV-02-C
NOT IN PTS

### AVDTP/SNK/INT/L2C/BM/BV-03-C
ACTS Steps:
1. `avdtp_init sink`
2. `tool_set_target_device_name PTS`
3. `tool_refresh_unique_id_using_bt_control`
4. `btc_connect`
5. `btc_disconnect`

### AVDTP/SNK/INT/L2C/EM/BV-01-C
1. `avdtp_init source`
2. `tool_set_target_device_name PTS`
3. `tool_refresh_unique_id_using_bt_control`
4. `sdp_init`
4. `btc_connect`
5. `sdp_connect_l2cap 0019 ERTM`

### AVDTP/SNK/INT/SIG/SMG/BV-05-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/INT/SIG/SMG/BV-07-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/INT/SIG/SMG/BV-09-C
ACTS Steps:
1. `avdtp_init sink`
2. `avdtp_get_connected_peers` - save this peer id
3. `avdtp_set_configuration [peer-id]`

### AVDTP/SNK/INT/SIG/SMG/BV-11-C
ACTS Steps:
1. `avdtp_init sink`
2. `avdtp_get_connected_peers` - save this peer id
3. `avdtp_get_configuration [peer-id]`

### AVDTP/SNK/INT/SIG/SMG/BV-13-C
ACTS Steps:
1. `avdtp_init sink`
2. `avdtp_get_connected_peers` - save this peer id
3. `avdtp_suspend_stream [peer-id]`
4. `avdtp_reconfigure_stream [peer-id]`

### AVDTP/SNK/INT/SIG/SMG/BV-15-C
ACTS Steps:
1. `avdtp_init sink`
2. Once connected run `tool_refresh_unique_id_using_bt_control` and verify that PTS is discovered
3. `sdp_init`
4. `avdtp_get_connected_peers` - save this peer id
5. `avdtp_get_capabilities [peer-id]`
6. `avdtp_set_configuration [peer-id]`
7. `avdtp_establish_stream [peer-id]`
8. `sdp_connect_l2cap 0019 BASIC`

### AVDTP/SNK/INT/SIG/SMG/BV-19-C
ACTS Steps:
1. `avdtp_init sink`
2. `avdtp_get_connected_peers` - save this peer id
3. `avdtp_start_stream [peer-id]`
4. `avdtp_release_stream [peer-id]`

### AVDTP/SNK/INT/SIG/SMG/BV-23-C
ACTS Steps:
1. `avdtp_init sink`
2. `avdtp_get_connected_peers` - save this peer id
3. `avdtp_set_configuration [peer-id]`
4. `avdtp_abort_stream [peer-id]`

### AVDTP/SNK/INT/SIG/SMG/BV-25-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SNK/INT/SIG/SMG/BV-28-C
ACTS Steps:
1. `avdtp_init sink`
2. [PTS Interaction] Confirm service capabilities reported to the upper tester

### AVDTP/SNK/INT/SIG/SMG/BV-31-C
ACTS Steps:
1. `avdtp_init sink`
2. `avdtp_get_connected_peers` - save this peer id
3. `avdtp_set_configuration [peer-id]`

### AVDTP/SNK/INT/SIG/SMG/ESR05/BV-13-C
ACTS Steps:
1. `avdtp_init sink`
2. `avdtp_get_connected_peers` - save this peer id
3. `avdtp_reconfigure_stream [peer-id]`

### AVDTP/SNK/INT/SIG/SMG/BI-30-C
ACTS Steps:
1. `avdtp_init sink`
2. `avdtp_get_connected_peers` - save this peer id
3. `avdtp_set_configuration [peer-id]`

### AVDTP/SNK/INT/SIG/SMG/BI-35-C
ACTS Steps:
1. `avdtp_init sink`
2. `avdtp_get_connected_peers` - save this peer id
3. `avdtp_set_configuration [peer-id]`

### AVDTP/SNK/INT/SIG/SMG/BI-36-C
ACTS Steps:
1. `avdtp_init sink`
2. `avdtp_get_connected_peers` - save this peer id
3. `avdtp_set_configuration [peer-id]`

### AVDTP/SRC/ACP/L2C/BM/BV-01-C
ACTS Steps:
1. `avdtp_init sink`

### AVDTP/SRC/ACP/L2C/EM/BV-02-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/BV-06-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/BV-08-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/BV-10-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/BV-12-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/BV-14-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/BV-16-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/BV-18-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/BV-20-C
ACTS Steps:
1. `btc_accept_pairing`
2. `avdtp_init source` 

### AVDTP/SRC/ACP/SIG/SMG/BV-22-C
ACTS Steps:
1. `btc_accept_pairing`
2. `avdtp_init source` 

### AVDTP/SRC/ACP/SIG/SMG/BV-24-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/BV-26-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/SIG/SMG/ESR05/BV-14-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/ACP/TRA/BTR/BI-01-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/INT/L2C/BM/BV-02-C
NOT IN PTS

### AVDTP/SRC/INT/L2C/BM/BV-03-C
ACTS Steps:
1. `avdtp_init source`
2. `tool_set_target_device_name PTS`
3. `tool_refresh_unique_id_using_bt_control`
4. `btc_connect`

### AVDTP/SRC/INT/L2C/EM/BV-01-C

### AVDTP/SRC/INT/SIG/SMG/BV-05-C
Note: Verify you are disconnected before the test starts
ACTS Steps:
1. `avdtp_init source`
2. `tool_set_target_device_name PTS`
3. `tool_refresh_unique_id_using_bt_control`
4. `btc_connect`

### AVDTP/SRC/INT/SIG/SMG/BV-07-C
ACTS Steps:
1. `avdtp_init source`


### AVDTP/SRC/INT/SIG/SMG/BV-09-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/INT/SIG/SMG/BV-11-C
ACTS Steps:
1. `avdtp_init source`
2. `avdtp_get_connected_peers` - save this peer id
3. `avdtp_get_configuration [peer-id]`

### AVDTP/SRC/INT/SIG/SMG/BV-13-C
ACTS Steps:
1. `avdtp_init source`
2. `avdtp_get_connected_peers` - save this peer id
3. `avdtp_suspend_stream [peer-id]`
4. `avdtp_reconfigure_stream [peer-id]`

### AVDTP/SRC/INT/SIG/SMG/BV-15-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/INT/SIG/SMG/BV-17-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/INT/SIG/SMG/BV-19-C
ACTS Steps:
1. `avdtp_init source`
2. `avdtp_get_connected_peers` - save this peer id
3. `avdtp_release_stream [peer-id]`


### AVDTP/SRC/INT/SIG/SMG/BV-21-C
ACTS Steps:
1. `avdtp_init source`
2. `avdtp_get_connected_peers` - save this peer id
3. `avdtp_suspend_stream [peer-id]`

### AVDTP/SRC/INT/SIG/SMG/BV-23-C
ACTS Steps:
1. `avdtp_init source`
2. `avdtp_get_connected_peers` - save this peer id
3. `avdtp_abort_stream [peer-id]`

### AVDTP/SRC/INT/SIG/SMG/BV-25-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/INT/SIG/SMG/BV-28-C
ACTS Steps:
1. `avdtp_init source`
2. [PTS Interaction] - Verify get all capabilities response and press OK

### AVDTP/SRC/INT/SIG/SMG/BV-31-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/INT/SIG/SMG/ESR05/BV-13-C
Note: This testcase requies you to send the reconfigure very quickly when PTS prompts for it
otherwise it fails.

ACTS Steps:
1. `avdtp_init source`
2. `avdtp_get_connected_peers` - save this peer id
3. `avdtp_reconfigure_stream 1062689294170098653`

### AVDTP/SRC/INT/SIG/SMG/BI-30-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/INT/SIG/SMG/BI-35-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/INT/SIG/SMG/BI-36-C
ACTS Steps:
1. `avdtp_init source`

### AVDTP/SRC/INT/TRA/BTR/BV-01-C
ACTS Steps:
1. `avdtp_init source`
