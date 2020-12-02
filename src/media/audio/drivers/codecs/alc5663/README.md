# ALC5663 Codec Driver

The ALC5663 (Realtek RT5663) is a headphone amplifier / codec, with the
following features:

  * Support for up to 32-bit, 192KHz, sterio signals.
  * Support for analog microphone recording on headset devices.
  * Support for plug detect, headsets with 3 or 4 buttons.

The ALC5663 is controlled via an I2C bus, and uses an I2S/PCM interface
for audio data.

  * Controlled off an I2C bus.
  * Audio streamed via an I2S/PCM bus.

This driver is currently just a stub, not actually capable of recording or
playing audio.

## Errata

Implementation is based on the Realtek ALC5663 datasheet (Revision 0.66,
2017-04-06). Some differences from the datasheet in the driver implementation
are as follows:

  * The datasheet states register addresses are 8-bits long (Table 16 and 17,
    page 28). In practice the register addresses are 16 bits long.
