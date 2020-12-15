# Simple DAI protocol driver

This example driver connects to a driver prodiving the DAI interface (DAI
driver) and provides an audio streaming interface for applications such that for
instance `audio-driver-ctl` can be used to test a new DAI driver.

To enable this test driver, it must be included in the build and in a board
driver. To add to the build include
board_bootfs_labels += ["//src/media/audio/drivers/dai-test"]
in the args.gn file or via fx set by adding to the fx set line
--args 'board_bootfs_labels += ["//src/media/audio/drivers/dai-test"]'.
To add to a board driver, for instance for astro define ENABLE_DAI_TEST in
//src/devices/board/drivers/astro/astro-audio.cc.
