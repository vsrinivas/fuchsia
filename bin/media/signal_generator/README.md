# Signal Generator tool

This directory contains a developer tool that generates and outputs audio
signals, via the Audio, AudioRenderer, GainControl and AudioOutputRoutingPolicy FIDL
interfaces.

### USAGE

    signal_generator
      [--chans=<NUM_CHANS>]
      [--rate=<FRAME_RATE>]
      [--int16 | --int24]
      [--sine[=<FREQ>] | --square[=<FREQ>] | --saw[=<FREQ>] | --noise]
      [--amp=<AMPL>]
      [--dur=<DURATION_SEC>]
      [--frames=<PACKET_SIZE>]
      [--wav[=<FILEPATH>]]
      [--gain=<GAIN_DB>]
      [--ramp]
      [--endgain=<TARGET_GAIN_DB>]
      [--rampdur=<RAMP_DURATION_MSEC>]
      [--sgain=<SYSTEM_GAIN_DB>]
      [--smute[=0|=1]]
      [--last | --all]
      [--help | --?]

These optional parameters are interpreted as follows:

    --chans=<NUM_CHANS>     Specify number of output channels (default 2)
    --rate=<FRAME_RATE>     Set output frame rate in Hertz (default 48000)
    --int16                 Emit signal as 16-bit integer (default float32)
    --int24                 Emit signal as 24-in-32-bit int (default float32)

    --sine[=<FREQ>]         Play sine of given frequency, in Hz (default 440.0)
    --square[=<FREQ>]       Play square wave (default 440.0 Hz)
    --saw[=<FREQ>]          Play rising sawtooth wave (default 440.0 Hz)
    --noise                 Play pseudo-random 'white' noise
                            In the absence of any of the above, sine is played.

    --amp=<AMPL>            Set signal amplitude (full-scale=1.0, default 0.5)

    --dur=<DURATION>        Set playback length, in seconds (default 2.0)
    --frames=<PACKET_SIZE>  Set packet size, in frames (default 480)

    --wav[=<FILEPATH>]      Save to file (default /tmp/signal_generator.wav)
                            Gain/mute settings do not affect .file contents.
                            24-bit signals are saved in left-justified int32s.

    --gain=<GAIN_DB>        Set AudioRenderer gain, in dB (default 0.0,
                            range [-160.0, +24.0]). If flag is absent, SetGain
                            is not called, so the stream plays at unity gain.

    --ramp                  Smoothly ramp gain to -75.0 dB, over the signal's
                            duration. Values for end-of-ramp gain and ramp
                            duration are overridden by '--endgain' and
                            '--rampdur', respectively. If '--gain' is not
                            specified, ramping starts at unity gain.
    --endgain=<GAIN_DB>     Ramp stream gain to the specified value (in dB),
                            instead of default -75.0 dB. (implies '--ramp')
    --rampdur=<DURATION_MS> Ramp stream Gain over the specified duration (in
                            milliseconds), rather than the signal's entire
                            length. (implies '--ramp')

    --sgain=<GAIN>          Set System Gain to [-160.0, 0.0] dB (default -12.0)
    --smute[=<0|1>]         Set System Mute (1=mute, 0=unmute, default 1)
                            Changes to System Gain/Mute persist after playback.

    --last                  Set 'Play to Most-Recently-Plugged' policy
    --all                   Set 'Play to All' policy
                            Changes to audio policy persist after playback.

    --help, --?             Show this message

### IMPORTANT NOTE

Developers can use this tool to manipulate two important systemwide audio
settings: system ("master") gain/mute and audio output routing.  Changes to
these settings affect all audio output on the system and continue to have effect
even after the signal_generator tool runs and exits.  Only use the '--sgain',
'--smute', '--last' or '--all' flags if you intend to change the system state in
a persistent, "sticky" way.
