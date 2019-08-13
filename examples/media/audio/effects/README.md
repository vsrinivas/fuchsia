# Audio Effects Example Module

This directory contains the sources for a loadable module that implements the ABIs and essential
logic of a Fuchsia 'Audio Effects' plugin. This includes an implementation of the basic 'C'
interface (`lib_audio_effects.cc`), a base class for effects (`effect_base.cc`/`effect_base.h`),
and three effects derived from that class (delay, swap, and rechannel --
`delay_effect.cc`/`delay_effect.h`, `swap_effect.h` and `rechannel_effect.h` respectively).
