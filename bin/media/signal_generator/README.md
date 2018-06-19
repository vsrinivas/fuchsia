 # Signal Generator tool

This directory contains a developer tool that generates and renders audio
signals, via the Audio and AudioRenderer2 FIDL interfaces.

### USAGE

	signal_generator
			[--chans=<NUM_CHANS>]
			[--rate=<FRAME_RATE>]
			[--int | --i]
			[--sine[=<FREQ>] | --square[=<FREQ>] | --saw[=<FREQ>] | --noise]
			[--amp=<AMPL>]
			[--dur=<DURATION>]
			[--ms=<MSEC>]
			[--wav[=<FILEPATH>]]
			[--rgain=<GAIN>]
			[--sgain=<GAIN>]
			[--last | --all]
			[--help | --?]

	--chans=<NUM_CHANS>	Specify number of output channels (default 2)
	--rate=<FRAME_RATE>	Set output frame rate in Hertz (default 48000)
	--int, --i		Emit signal as 16-bit integer (default float32)

	--sine[=<FREQ>]		Play sine of given frequency, in Hz (default 400)
	--square[=<FREQ>]	Play square wave (default 400 Hz)
	--saw[=<FREQ>]		Play rising sawtooth wave (default 400 Hz)
	--noise			Play pseudo-random 'white' noise
			In absence of --square, --saw or --noise, a sine is played

	--amp=<AMPL>		Set signal amplitude (full-scale=1.0, default 0.5)

	--dur=<DURATION>	Set playback length, in seconds (default 2)
	--ms=<MSEC>		Set data buffer size, in milliseconds (default 10)

	--wav[=<FILEPATH>]	Save to .wav file (default /tmp/signal_generator.wav)
			Note: .wav file contents are unaffected by gain settings

	--rgain=<GAIN>		Set Renderer gain to [-160.0, 24.0] dB (default 0.0)
	--sgain=<GAIN>		Set System gain to [-160.0, 0.0] dB (default -12.0)
			Note: changes to System gain persist after playback.

	--last			Set 'Play to Most-Recently-Plugged' policy
	--all			Set 'Play to All' policy
			Note: changes to audio policy persist after playback.

	--help, --?		Show this message

### IMPORTANT NOTE

Developers can use this tool to manipulate two important systemwide audio
settings: system ("master") gain and audio output routing.  Changes to these
settings affect all audio output on the system and continue to take effect even
after the signal_generator tool runs and exits.  Only use `--sgain`, `--last` or
`--all` if you intend to change the system state in a 'sticky' way.