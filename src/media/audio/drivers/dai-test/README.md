# Simple DAI protocol driver

This example driver connects to a driver prodiving the DAI interface (DAI
driver) and provides an audio streaming interface for applications such that for
instance `audio-driver-ctl` can be used to test a new DAI driver.

To enable this test driver, it must be included via a board driver. For instance
definding ENABLE_DAI_TEST in //src/devices/board/drivers/astro/astro-audio.cc
adds this driver to the astro board driver.
