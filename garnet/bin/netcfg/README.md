# netcfg

netcfg is the policy manager for netstack.

Currently, netcfg can be used to override the four word fuchsia device name,
derived from the MAC address of the first network interface (gethostbyname,
mDNS, uname -a, and DeviceSettingsManager will all reflect the changed name).

Modify netcfg/config/default.json and add a key for device_name:

```
{
  "device_name": "my-cool-new-device"
}
```
