# Wave File Recorder Utility App

This directory contains a utility application that uses the AudioCapturer
interface and the WavWriter class to record a waveform audio file.

### USAGE

    wav_recorder [options] <filename>
    Record an audio signal from the specified source, to a .wav file.

    Valid options:

      By default, use the preferred input device
    --loopback            Capture final-mix output from the preferred output device

      By default, use device-preferred channel count and frame rate, in 16-bit integer samples
    --chans=<NUM_CHANS>   Specify the number of channels (in [1, 8])
    --rate=<FRAME_RATE>   Specify the capture frame rate (Hz in [1000, 192000])
    --float               Record and save as 32-bit float
    --int24               Record and save as left-justified 24-in-32 int ('padded-24')
    --packed24            Record as 24-in-32 'padded-24', but save as 'packed-24'

      By default, don't set AudioCapturer gain and mute (unity 0 dB, unmuted)
    --gain[=<GAIN_DB>]    Set stream gain (dB in [-160, +24]; 0.0 if only '--gain' is provided)
    --mute[=<0|1>]        Set stream mute (0=Unmute or 1=Mute; Mute if only '--mute' is provided)

      By default, packet-by-packet ('synchronous') mode is used
    --async               Capture using sequential-buffer ('asynchronous') mode

    --v                   Be verbose; display per-packet info
    --help, --?           Show this message
