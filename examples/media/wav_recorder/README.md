# Wave File Recorder Example App

This directory contains an example application that uses the AudioCapturer
interface and the WavWriter class to record a waveform audio file.

### USAGE
    wav_recorder [options] <filename>
    Record an audio signal from the specified source, to a .wav file.

    Valid options:
    --v                   : Be verbose; display per-packet info

      Default is to not set AudioIn gain, leaving the default 0 dB (unity)
    --gain=<gain_db>      : Set the stream's gain (range [-160.0, +24.0])

      Default is to capture from the preferred input device
    --loopback            : Capture final-mix-output from preferred output device

      Default is to use the device's preferred frame rate
    --rate=<rate>         : Specify the capture frame rate (range [1000, 192000])

      Default is to use the device's preferred number of channels
    --chans=<count>       : Specify the number of channels captured (range [1, 8])

      Default is to record and save as 16-bit integer
    --float               : Record and save as 32-bit float
    --int24               : Record and save as 24-in-32 integer (left-justified 'padded-32')
    --packed24            : Record as 24-in-32 'padded-32', but save as 'packed-24'

      Default is to record in 'synchronous' (packet-by-packet) mode
    --async               : Capture using 'asynchronous' mode
