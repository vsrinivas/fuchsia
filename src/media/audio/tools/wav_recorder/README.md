# Wave File Recorder Utility App

This directory contains a command-line utility application that uses the
AudioCapturer interface and the WavWriter class to record a waveform audio file.

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
    --float64              Record and save as 64-bit float

      By default, don't set AudioCapturer gain and mute (unity 0 dB and unmuted)
    --gain[=<GAIN_DB>]     Set stream gain, in dB (min -160.0, max +24.0, default 0.0)
    --mute[=<0|1>]         Set stream mute (0=Unmute or 1=Mute; Mute if only '--mute' is provided)

      By default, use sequential-buffer ('asynchronous') mode
    --sync                 Capture using packet-by-packet ('synchronous')) mode

      Use the default reference clock unless specified otherwise
    --flexible-clock       Use the 'flexible' reference clock provided by the Audio service
    --monotonic-clock      Set the local system monotonic clock as reference for this stream
    --custom-clock         Use a custom clock as this stream's reference clock
    --rate-adjust[=<PPM>]  Run faster/slower than local system clock, in parts-per-million
                           (min -1000, max 1000; -75 if unspecified).
                           Implies '--custom-clock' if '--monotonic-clock' is not specified

      By default, capture audio using packets of 100.0 msec
    --packet-ms=<MSECS>    Specify the duration (in milliseconds) of each capture packet
                           (min 1.0, max 500.0)

      By default, capture until a key is pressed
    --duration[=<SECS>]    Stop recording after a fixed duration (or keystroke)
                           (min 0.0, max 86400.0, default 2.0)

    --ultrasound           Capture from an ultrasound capturer

    --v                    Display per-packet information
    --help, --?            Show this message
