# Command Line Audio Watcher/Listener Tool (audio_listener)

This tool shows available information about render and capture audio usages. It
uses ActivityReporter, Usage(Reporter|Watcher), UsageGain(Reporter|Listener), as
well as usage VolumeControls available from the AudioCore interface.

Current per-usage data is updated in real-time, for all render/capture usages:

  - Activity (whether the usage is active), per fuchsia.media.ActivityReporter
  - State (Normal/Ducked/Muted), per fuchsia.media.UsageWatcher
  - Volume (0.0 - 1.0), from fuchsia.media.AudioCore/BindUsageVolumeControl
  - Gain (dB), per fuchsia.media.UsageGainListener

To switch between Activity | State | Volume | Gain display modes, press arrow keys
(up | left | down | right for Activity | State | Volume | Gain respectively), or
numerical keys 1-4 (handy when arrow keys are unavailable).

In Activity mode, for every usage a six-letter abbreviation is displayed iff it is
active: Backgd, Media, Interr, Foregd, SysAgt, Comms.

In State, Volume and Gain modes, the first letter of each usage is shown alongside
that usage's information.

To quit the audio_listener tool, press Q or [Enter].
