# Wave File Recorder Utility App

This directory contains a utility application that uses the AudioCapturer interface and the
WavWriter class to record a waveform audio file.

### USAGE

    wav_recorder [options] <filename>
    Record an audio signal from the specified source to a .wav file.

    Valid options:

      By default, use the preferred input device
    --loopback             Capture final-mix output from the preferred output device

      By default, use device-preferred channel count and frame rate, in 32-bit float samples
    --chans=<NUM_CHANS>    Specify the number of channels (min 1, max 8)
    --rate=<rate>          Specify the capture frame rate, in Hz (min 1000, max 192000)
    --int24                Record and save as left-justified 24-in-32 int ('padded-24')
    --packed24             Record as 24-in-32 'padded-24'; save as 'packed-24'
    --int16                Record and save as 16-bit integer

      By default, don't set AudioCapturer gain and mute (unity 0 dB, unmuted)
    --gain[=<GAIN_DB>]     Set stream gain, in dB (min -160.0, max +24.0, default 0.0)
    --mute[=<0|1>]         Set stream mute (0=Unmute, 1=Mute, if only '--mute' then Mute)

      By default, use packet-by-packet ('synchronous') mode
    --async                Capture using sequential-buffer ('asynchronous') mode

      By default, capture audio using packets of 100.0 msec
    --packet-ms=<MSECS>    Specify the duration (in milliseconds) of each capture packet
                           Minimum packet duration is 1.0 millisec

      By default, capture until a key is pressed
    --duration[=<SECS>]    Specify a fixed duration rather than waiting for keystroke
                           (min 0.0, max 86400.0, default 2.0)

    --v                    Display per-packet information
    --help, --?            Show this message
