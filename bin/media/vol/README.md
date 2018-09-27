# Command Line Volume Control Tool (vol)

This tool queries and changes the gain/mute/AGC settings for audio devices.
It also displays the token and UID identifiers for current devices and
notes which devices are 'default'. Changes made with the 'vol' tool persist
after the tool is closed. Note: Device gain/mute are not the same as System
gain/mute -- although today we treat any explicit change in System gain/mute as
a Device gain/mute change applied to all devices.

```
    vol <args>
        --help           show this message
        --show           show system audio status
        --token=<id>     select the device by token
        --uid=<uid>      select the device by partial UID
        --input          select the default input device
        --gain=<db>      set system audio gain
        --mute=(on|off)  mute/unmute system audio
        --agc=(on|off)   enable/disable AGC

    Given no arguments, vol waits for the following keystrokes
        +            increase system gain
        -            decrease system gain
        m            toggle mute
        a            toggle agc
        enter        quit
```
