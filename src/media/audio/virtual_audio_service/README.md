# Virtual Audio Service

The intention of the Virtual Audio Service is to provide an easy-to-use
interface for configuring and controlling virtual audio devices. The intended
clients for this service are tests and diagnostic utilities.

The Virtual Audio Service is located in the "virtual_audio_service" binary,
which should only be included on fuchsia builds that include the
"media/tests" component group.

Essentially, this service accepts binding requests for fuchsia.virtualaudio
interfaces (Control, Input, Output), then forwards the server side of those
connections onward to the virtual audio controller driver
(virtual_audio_driver.so). The driver then fully implements these FIDL
interfaces.

For more information on the FIDL interface, or our plans for using virtual audio
to more fully test the audio subsystem, see virtual_audio.fidl or the README for
virtual_audio_driver.
