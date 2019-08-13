This library exists to support libfs-backed implementations of
fuchsia.hardware.pty.Device.  Using libfs as a base lets us avoid reimplementing
the subtle details of the fuchsia.io.File interface.
