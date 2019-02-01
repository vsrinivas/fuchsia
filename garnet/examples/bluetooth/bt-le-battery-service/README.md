# BT LE Battery Service 

This example demonstrates how to publish the system's battery level in a GATT battery service.
When run, it adds the service to the local device database. It then responds to clients
that make requests to this GATT service.


## Usage:

Run the service with `$ bt-le-battery-service`.
To enable advertisment of the Battery Service (BAS) on the host run
`$ bt-le-peripheral --name "FX BLE BATTERY" --service BAS`.
