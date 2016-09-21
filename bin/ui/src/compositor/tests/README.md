# Mozart API Tests

This directory contains test cases that exercise the Mozart Compositor APIs
through IPC as a client would.

These tests need a virtual framebuffer to execute.  Run them like this:

$ testing/xvfb.py out/Debug mojo_run mojo:compositor_apptests
  --shell-path out/Debug/mojo_shell \
  --args-for="mojo:native_viewport_service --use-test-config"
