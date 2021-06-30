# WLAN SME

This library implements IEEE 802.11 SME functionality for both FullMAC and SoftMAC devices.

SME instances are created and hosted by a parent ['wlanstack'](../../wlanstack/) instance when WLAN interfaces start.

## AP Implementation

Support for Access Points is currently fairly limited.

## Client Implementation

Our client can connect to Open/WEP/WPA1/2/3 APs.

### ['Top-level client'](./src/client/mod.rs)

- Responds to SME.fidl messages and initiates scans/connects/etc. as a result.
- Dispatches incoming MLME messages to the scan scheduler or state machine as appropriate.
- Initiates connections in response to completed join scans.

### ['Client state machine'](./src/client/state/mod.rs)

- Manages all steps of a connection from authentication through completed association.
- Uses SME and EAPOL libraries to handle various authentication exchanges.
- Manages EAPOL/RSNA in the underlying ['link state machine'](./src/client/state/link_state.rs)