# Signal Generator tool

This directory contains a developer tool that generates and outputs audio signals, via the Audio,
AudioCore, AudioRenderer, VolumeControl and GainControl FIDL protocols.

### USAGE

    signal_generator
      [--chans=<NUM_CHANS>]
      [--int16 | --int24]
      [--rate=<FRAME_RATE>]
      [--usage=<RENDER_USAGE>]
      [--sine[=<FREQ>] | --square[=<FREQ>] | --saw[=<FREQ>] | --noise]
      [--dur=<DURATION_SEC>]
      [--amp=<AMPL>]
      [--wav[=<FILEPATH>]]
      [--pts]
      [--threshold=<SECS>]
      [--frames=<FRAMES>]
      [--num-bufs=<BUFFERS>]
      [--gain[=<STREAM_GAIN_DB>]]
      [--mute[=<0|1>]]
      [--ramp]
      [--end-gain=<RAMP_END_DB>]
      [--ramp-dur=<RAMP_DURATION_MSEC>]
      [--usage-gain[=<USAGE_GAIN_DB>]]
      [--usage-vol[=<USAGE_VOLUME>]]
      [--v]
      [--help | --?]

These optional parameters are interpreted as follows:

      By default, stream format is 2-channel, float32, 48000 Hz frame rate, MEDIA usage
    --chans=<NUM_CHANS>      Specify number of channels
    --int16                  Use 16-bit integer samples
    --int24                  Use 24-in-32-bit integer samples (left-justified 'padded-24')
    --rate=<FRAME_RATE>      Set frame rate in Hz
    --usage=<RENDER_USAGE>   Set stream render usage. RENDER_USAGE must be one of:
                             BACKGROUND, MEDIA, INTERRUPTION, SYSTEM_AGENT, COMMUNICATION

      By default, signal is a sine wave. If no frequency is provided, 440.0 Hz is used
    --sine[=<FREQ>]          Play sine wave at given frequency (Hz)
    --square[=<FREQ>]        Play square wave at given frequency
    --saw[=<FREQ>]           Play rising sawtooth wave at given frequency
    --noise                  Play pseudo-random 'white' noise

      By default, play signal for 2.0 seconds, at amplitude 0.25
    --dur=<DURATION_SECS>    Set playback length, in seconds
    --amp=<AMPL>             Set amplitude (silence=0.0, full-scale=1.0)

    --wav[=<FILEPATH>]       Save to .wav file (default '/tmp/signal_generator.wav')

      Subsequent settings (e.g. gain, timestamps) do not affect .wav file contents

      By default, submit data in non-timestamped buffers of 480 frames and 1 VMOs.
    --pts                    Apply presentation timestamps (units: frames)
    --threshold[=<SECS>]     Set PTS discontinuity threshold, in seconds (default 0.0)
    --frames=<FRAMES>        Set packet size, in frames
    --num-bufs=<BUFFERS>     Set the number of payload buffers to use

      By default, do not set AudioRenderer gain/mute (unity 0.0 dB, unmuted, no ramping)
    --gain[=<GAIN_DB>]       Set stream gain, in dB (min -160.0, max 24.0, default 0.0)
    --mute[=<0|1>]           Set stream mute (0=Unmute or 1=Mute; Mute if only '--mute' is provided)
    --ramp                   Smoothly ramp gain from initial value to target -75.0 dB by end-of-signal
                             If '--gain' is not provided, ramping starts at unity stream gain (0.0 dB)
    --end-gain=<TARGET_DB>   Set a different ramp target gain (dB). Implies '--ramp'
    --ramp-dur=<MSECS>       Set a specific ramp duration in milliseconds. Implies '--ramp'

      By default, both volume and gain for this RENDER_USAGE are unchanged
    --usage-gain[=<DB>]      Set render usage gain, in dB (min -160.0, max 0.0, default 0.0)
    --usage-vol[=<VOLUME>]   Set render usage volume (min 0.0, max 1.0, default 0.50)

    --v                      Display per-packet information
    --help, --?              Show this message

### IMPORTANT NOTE

Developers can use this tool to change systemwide render-usage volume and gain. These changes
persist beyond this tool's invocation.
