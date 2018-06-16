# Command Line Volume Control Tool (vol)

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
