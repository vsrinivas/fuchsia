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
