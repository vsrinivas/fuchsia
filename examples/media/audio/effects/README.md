# Example Audio Effects Module

This directory contains the sources for a loadable module that implements the ABIs and essential
logic of a Fuchsia 'Audio Effects' plugin. This includes an implementation of the basic 'C'
interface (`dfx_lib.cc`), a base class for effects (`dfx_base.cc`/`dfx_base.h`), and three effects
derived from that class (delay, swap, and rechannel -- `dfx_delay.cc`/`dfx_delay.h`, `dfx_swap.h`
and `dfx_rechannel.h` respectively).
