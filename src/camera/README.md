# Fuchsia Camera Stack
While camera code follows the Fuchsia coding guide, we have added additional
stylistic rules to ensure that all camera code remains consistent.

----
## Code Layout

Camera code is layed out in the following fashion. Note that some code does not
obey this layout; this represents the planned organization.
```
camera
├── camera_manager
├── drivers
│   ├── camera_controller <- Provides a camera driver interface for the ISP and other hw
│   ├── common            <- for drivers that are related to cameras, like mipi
│   ├── hw_accel          <- For all hardware accelerators, scalars, etc.
│   ├── isp               <- For code relating to the actual isp block
│   │   ├── <isp_name>    <- Each ISP has a folder for device specific code
│   │   └── modules       <- shared ISP logic blocks go in modules
│   ├── sensors           <- For image sensors
│   ├── virtual           <- For virtual cameras, other mock devices
│   └── usb               <- For usb cameras implementing UVC
├── examples              <- Any examples of camera usages
└── e2e_tests             <- End to end tests of the camera stack



----
## Tests

###Organization

With the exception of end-to-end tests, all camera tests shall be located in a
```test``` folder next to the code it tests.
The test source shall be named ```<object-of-test>-test.cc```.
The ```meta``` folder for the test is located in the ```test``` directory.

Example:
```
bar
├── BUILD.gn
├── foo.cc
└── foo.h
   └── test
        ├── foo_test.cc
        ├── BUILD.gn
        └── meta
            └── foo_test.cmx
```

### Automation

Tests shall be automated in one of 3 ways:

* **Unit Tests** and **Integration Tests** shall be made into components that
are part of the ```camera_full_test``` test package, defined in
```camera/BUILD.gn```.  These tests should be able to be run in Qemu.
* **Driver Unit Tests** are built into drivers, and will be tested by <TBD>.
* **Debug interfaces** shall provide fidl interfaces, which will be connected
to by test programs.  These programs shall be included in the
```camera_full_on_device_test``` test package, defined in ```camera/BUILD.gn```.
All such tests shall operate on any Fuchsia platform, and shall only run tests
on appropriate devices.

Tests should be written using the [gtest](https://fuchsia.googlesource.com/third_party/googletest/+/refs/heads/master/googletest/docs/primer.md)
framework.


