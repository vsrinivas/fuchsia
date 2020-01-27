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
      [--frames=<PACKET_SIZE>]
      [--num-bufs=<NUM_BUFFERS>]
      [--pts]
      [--threshold=<SECS>]
      [--gain[=<STREAM_GAIN_DB>]]
      [--mute[=<0|1>]]
      [--ramp]
      [--end-gain=<TARGET_GAIN_DB>]
      [--ramp-dur=<RAMP_DURATION_MSEC>]
      [--usage-gain[=<GAIN_DB>]]
      [--usage-vol[=<VOLUME>]]
      [--settings<=ENABLED>]
      [--help | --?]

These optional parameters are interpreted as follows:

      By default, set stream format to 2-channel float32 at 48000 Hz with a MEDIA usage
    --chans=<NUM_CHANS>      Specify number of channels
    --int16                  Use 16-bit integer samples
    --int24                  Use 24-in-32-bit integer samples (left-justified 'padded-24')
    --rate=<FRAME_RATE>      Set frame rate in Hz
    --usage=<RENDER_USAGE>   Set stream render usage. RENDER_USAGE must be one of:
                             BACKGROUND, MEDIA, INTERRUPTION, SYSTEM_AGENT, COMMUNICATION

      By default, signal is a 440.0 Hz sine wave
    --sine[=<FREQ>]          Play sine wave at given frequency (Hz)
    --square[=<FREQ>]        Play square wave at given frequency
    --saw[=<FREQ>]           Play rising sawtooth wave at given frequency
    --noise                  Play pseudo-random 'white' noise
      If no frequency is provided (e.g. '--square'), 440.0 Hz is used

      By default, signal plays for 2.0 seconds, at amplitude 0.25
    --dur=<DURATION_SECS>    Set playback length in seconds
    --amp=<AMPL>             Set amplitude (full-scale=1.0, silence=0.0)

    --wav[=<FILEPATH>]       Save to .wav file ('/tmp/signal_generator.wav' if only '--wav' is
                             provided)

      Subsequent settings (e.g. gain, timestamps) do not affect .wav file contents

      By default, submit data in non-timestamped buffers of 480 frames and 1 VMOs.
    --frames=<FRAMES>        Set data buffer size in frames 
    --num-bufs=<NUM_BUFFERS> Set the number of payload buffers to use 
    --pts                    Apply presentation timestamps (units: frames)
    --threshold[=<SECS>]     Set PTS discontinuity threshold, in seconds (0.0, if unspecified)

      By default, AudioRenderer gain and mute are not set (unity 0.0 dB unmuted, no ramping)
    --gain[=<GAIN_DB>]       Set stream gain (dB in [-160.0, 24.0]; 0.0 if only '--gain' is
                             provided)
    --mute[=<0|1>]           Set stream mute (0=Unmute or 1=Mute; Mute if only '--mute' is
                             provided)
    --ramp                   Smoothly ramp gain from initial value to a target -75.0 dB by
                             end-of-signal. If '--gain' is not provided, ramping starts at unity
                             stream gain (0.0 dB)
    --end-gain=<GAIN_DB>     Set a different ramp target gain (dB). Implies '--ramp'
    --ramp-dur=<DURATION_MS> Set a specific ramp duration in milliseconds. Implies '--ramp'

      By default, both volume and gain for this RENDER_USAGE are unchanged
    --usage-gain[=<GAIN_DB>] Set render usage gain (dB in [-160.0, 0.0]; 0.0 if only '--usage-gain'
                             is provided)
    --usage-vol[=<VOLUME>]   Set render usage volume ([0.0, 1.0]; 0.50 if only '--usage-vol' is
                             provided)

      By default, changes to audio device settings are persisted
    --settings[=<0|1>]       Enable/disable creation/update of device settings
                             (0=Disable, 1=Enable; 0 is default if only '--settings' is provided)

    --help, --?              Show this message

### IMPORTANT NOTE

Developers can use this tool to change systemwide render-usage volume and gain, and to
enable/disable systemwide creation/update of settings files for audio input and output devices.
These changes persist beyond this tool's invocation.
