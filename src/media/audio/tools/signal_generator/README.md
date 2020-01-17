# Signal Generator tool

This directory contains a developer tool that generates and outputs audio signals, via the Audio,
AudioRenderer, and GainControl FIDL protocols.

### USAGE

    signal_generator
      [--chans=<NUM_CHANS>]
      [--int16 | --int24]
      [--rate=<FRAME_RATE>]
      [--sine[=<FREQ>] | --square[=<FREQ>] | --saw[=<FREQ>] | --noise]
      [--dur=<DURATION_SEC>]
      [--amp=<AMPL>]
      [--wav[=<FILEPATH>]]
      [--frames=<PACKET_SIZE>]
      [--nbufs=<NUM_BUFFERS>]
      [--pts]
      [--threshold=<SECS>]
      [--gain[=<STREAM_GAIN_DB>]]
      [--mute[=<0|1>]]
      [--ramp]
      [--endgain=<TARGET_GAIN_DB>]
      [--rampdur=<RAMP_DURATION_MSEC>]
      [--settings<=ENABLED>]
      [--help | --?]

These optional parameters are interpreted as follows:

      By default, set stream format to 2-channel float32 at 48000 Hz
    --chans=<NUM_CHANS>     Specify number of channels
    --int16                 Use 16-bit integer samples
    --int24                 Use 24-in-32-bit integer samples (left-justified 'padded-24')
    --rate=<FRAME_RATE>     Set frame rate in Hz

      By default, signal is a 440.0 Hz sine wave
    --sine[=<FREQ>]         Play sine wave at given frequency (Hz)
    --square[=<FREQ>]       Play square wave at given frequency
    --saw[=<FREQ>]          Play rising sawtooth wave at given frequency
    --noise                 Play pseudo-random 'white' noise
      If no frequency is provided (e.g. '--square'), 440.0 Hz is used

      By default, signal plays for 2.0 seconds, at amplitude 0.25
    --dur=<DURATION_SEC>    Set playback length in seconds
    --amp=<AMPL>            Set amplitude (full-scale=1.0, silence=0.0)

    --wav[=<FILEPATH>]      Save to .wav file ('/tmp/signal_generator.wav' if only '--wav' is provided)
      Subsequent settings (e.g. gain) do not affect file contents

      By default, submit data in non-timestamped buffers of 480 frames using a single VMO.
    --frames=<FRAMES>       Set data buffer size in frames
    --nbufs=<NUM_BUFFERS>   Set the number of payload buffers to use
    --pts                   Apply presentation timestamps (units: frames)
    --threshold[=<SECS>]    Set PTS discontinuity threshold, in seconds (0.0, if unspecified)

      By default, AudioRenderer gain and mute are not set (unity 0 dB, unmuted, no ramping)
    --gain[=<GAIN_DB>]      Set stream gain (dB in [-160.0, 24.0]; 0.0 if only '--gain' is provided)
    --mute[=<0|1>]          Set stream mute (0=Unmute or 1=Mute; Mute if only '--mute' is provided)
    --ramp                  Smoothly ramp gain from initial value to a target -75.0 dB by end-of-signal
                            If '--gain' is not provided, ramping starts from unity gain
    --endgain=<GAIN_DB>     Set a different ramp target gain (dB). Implies '--ramp'
    --rampdur=<DURATION_MS> Set a specific ramp duration in milliseconds. Implies '--ramp'

      By default, changes to audio device settings are persisted
    --settings[=<0|1>]      Enable/disable creation/update of device settings
                            (0=Disable, 1=Enable; 0 is default if only '--settings' is provided)

    --help, --?             Show this message

### IMPORTANT NOTE

Developers can use this tool to enable/disable systemwide creation/update of settings
files for audio input and output devices. This change persists beyond the invocation.
