# Media-Client Sine-Wave Example App

This directory contains an application that plays a 2-second sine wave to the
default audio output, using the C-based Media Client API.

This example uses almost all the Media Client APIs, but takes a very basic
approach to playing audio. It does not use timestamps for packets after the
first one, nor does it consider devices other than the default or take into
account the preferred format of that device.

### USAGE

  media_client_sine
