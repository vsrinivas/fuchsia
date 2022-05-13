# input_pipeline > Sensors > Atlas Touchpad

## Modes of operation

### Default (mouse mode)

The Atlas touchpad operates in mouse mode at boot time. In this mode, the
input-pipeline sees:

* `movement_x` and `movement_y`: X and Y motion
  * motion is reported in "counts" (see below for translation from counts to
    well-known units)
  * positive motion is towards the right and towards the user
* `pressed_buttons`:
  * for a tap: `Some([1])` followed by `Some([])`, regardless of which side of
    the touchpad is tapped
  * for a left-side click: `Some([1])` followed by `Some([])`
  * for a right-side click: `Some([2])` followed by `Some([])`
* an effective resolution of ~12 counts-per-millimeter (~300 counts per inch)
* reports every 8 msec intervals (125 HZ) when a finger is in motion on the
  touchpad

Example [`MouseInputReport`][mouse-input-report-fidl]
```
MouseInputReport {
    movement_x: Some(2),
    movement_y: Some(35),
    scroll_v: None,
    scroll_h: None,
    pressed_buttons: Some([]),
    position_x: None,
    position_y: None,
    unknown_data: None,
    __non_exhaustive: ()
})
```

### Switching

The touchpad can be switched into touchpad mode (and back) by invoking
the [SetFeatureReport API][set-feature-report-api], with `report.input_mode`
set to [the appropriate value][touch-configuration-input-mode].

### Touchpad mode

In touchpad mode, the input-pipeline sees:

* `contacts`: an array where every finger in contact with the touchpad is
  reported with
  * `contact_id`: a consistent identifier for the duration of that finger's
    contact with the touchpad
  * `position_x` and `position_y`: X and Y position
    * reported in micrometers
    * max is at the right edge, and the edge near the user
  * `contact_width` and `contact_height`: width and height of the portion
    of the finger in contact with the touchpad
    * reported in unknown units
    * the descriptor says micrometers, but the actual value seems smaller
      * an adult palm pressed hard against the touchpad is reported to have a
        `contact_width` of about 11.1 cm (0.43 inches), which is far too small
      * an adult thumb pressed hard against the touchpad is reported to have a
        `contact_width` of about 7.7 cm (0.30 inches), which is also too small
  * `pressure` in unspecified units
* `pressed_buttons`:
  * for a tap: `Some([])`; a tap does _not_ populate the `pressed_buttons` array
  * for a click: `Some([1])` followed by `Some([])`, regardless of which side of
    the touchpad is clicked
* Reports every 8 msec intervals (125 HZ) when a finger is in contact with
  the touchpad

Example [`TouchInputReport`][touch-input-report-fidl]
```
TouchInputReport {
    contacts: Some([ContactInputReport {
        contact_id: Some(0),
        position_x: Some(66637),
        position_y: Some(33604),
        pressure: Some(7904),
        contact_width: Some(2953),
        contact_height: Some(3230),
        unknown_data: None,
        __non_exhaustive: () }]),
    pressed_buttons: Some([]),
    unknown_data: None,
    __non_exhaustive: ()
}
```

## HID Descriptor

For the raw HID descriptor data, see [atlas_touchpad_descriptor.hex](atlas_touchpad_descriptor.hex).

This decoding is from https://eleccelerator.com/usbdescreqparser/, augmented
with some manual decoding using new usage data in
[HID Usage Tables v1.3][hid-usage-tables-1-3], and the
[Windows Precision Touchpad documentation][precision-touchpad-docs].

Notes
* This describes many fields (e.g. Scan Time) that we don't surface in
  `TouchInputReport` today.
* Many of the fields surfaced (e.g. `contact_width`, `pressure`) are _not_
  required for Windows Precision Touchpads.
* The descriptor's choice of units is self-contradictory:
  `Unit (System: English Linear, Length: Centimeter)` is contradictory, since
  centimeters aren't a unit of measure in the English system.
* The units and ranges in the HID descriptor differ from those seen by the
  input-pipeline, because of translation done by the driver stack. See
  * [Unit translation for descriptors](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/input/lib/hid-input-report/axis.cc;l=16?q=hidunittollcppunit&ss=fuchsia%2Ffuchsia)
  * [Value translation for reports](https://cs.opensource.google/fuchsia/fuchsia/+/main:zircon/system/ulib/hid-parser/units.cc;l=237?q=hid-parser%2Funits.cc&ss=fuchsia%2Ffuchsia)

```
0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
0x09, 0x02,        // Usage (Mouse)
0xA1, 0x01,        // Collection (Application)         <-- mouse mode
0x85, 0x01,        //   Report ID (1)
0x09, 0x01,        //   Usage (Pointer)
0xA1, 0x00,        //   Collection (Physical)
0x05, 0x09,        //     Usage Page (Button)
0x19, 0x01,        //     Usage Minimum (0x01)
0x29, 0x02,        //     Usage Maximum (0x02)
0x15, 0x00,        //     Logical Minimum (0)
0x25, 0x01,        //     Logical Maximum (1)
0x75, 0x01,        //     Report Size (1)
0x95, 0x02,        //     Report Count (2)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x95, 0x06,        //     Report Count (6)
0x81, 0x03,        //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
0x09, 0x30,        //     Usage (X)
0x09, 0x31,        //     Usage (Y)
0x15, 0x81,        //     Logical Minimum (-127)
0x25, 0x7F,        //     Logical Maximum (127)
0x75, 0x08,        //     Report Size (8)
0x95, 0x02,        //     Report Count (2)
0x81, 0x06,        //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
0x75, 0x08,        //     Report Size (8)
0x95, 0x05,        //     Report Count (5)
0x81, 0x03,        //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0xC0,              //   End Collection
0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
0x09, 0x01,        //   Usage (0x01)
0x85, 0x0E,        //   Report ID (14)
0x09, 0xC5,        //   Usage (0xC5)
0x15, 0x00,        //   Logical Minimum (0)
0x26, 0xFF, 0x00,  //   Logical Maximum (255)
0x75, 0x08,        //   Report Size (8)
0x95, 0x04,        //   Report Count (4)
0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
0xC0,              // End Collection
0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
0x09, 0x01,        // Usage (0x01)
0xA1, 0x01,        // Collection (Application)
0x85, 0x5C,        //   Report ID (92)
0x09, 0x01,        //   Usage (0x01)
0x95, 0x0B,        //   Report Count (11)
0x75, 0x08,        //   Report Size (8)
0x81, 0x06,        //   Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
0x85, 0x0D,        //   Report ID (13)
0x09, 0xC5,        //   Usage (0xC5)
0x15, 0x00,        //   Logical Minimum (0)
0x26, 0xFF, 0x00,  //   Logical Maximum (255)
0x75, 0x08,        //   Report Size (8)
0x95, 0x04,        //   Report Count (4)
0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
0x85, 0x0C,        //   Report ID (12)
0x09, 0xC6,        //   Usage (0xC6)
0x96, 0x48, 0x03,  //   Report Count (840)
0x75, 0x08,        //   Report Size (8)
0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
0x85, 0x0B,        //   Report ID (11)
0x09, 0xC7,        //   Usage (0xC7)
0x95, 0x42,        //   Report Count (66)
0x75, 0x08,        //   Report Size (8)
0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
0xC0,              // End Collection
0x05, 0x0D,        // Usage Page (Digitizer)
0x09, 0x05,        // Usage (Touch Pad)
0xA1, 0x01,        // Collection (Application)         <-- touchpad mode
0x85, 0x04,        //   Report ID (4)
0x05, 0x09,        //   Usage Page (Button)
0x09, 0x01,        //   Usage (Button 1)
0x15, 0x00,        //   Logical Minimum (0)
0x25, 0x01,        //   Logical Maximum (1)
0x75, 0x01,        //   Report Size (1)
0x95, 0x01,        //   Report Count (1)
0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x75, 0x01,        //   Report Size (1)
0x95, 0x03,        //   Report Count (3)
0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x0D,        //   Usage Page (Digitizer)
0x09, 0x54,        //   Usage (Contact Count)
0x25, 0x0F,        //   Logical Maximum (15)
0x75, 0x04,        //   Report Size (4)
0x95, 0x01,        //   Report Count (1)
0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x0D,        //   Usage Page (Digitizer)
0x09, 0x56,        //   Usage (Scan Time)
0x15, 0x00,        //   Logical Minimum (0)
0x25, 0x64,        //   Logical Maximum (100)
0x55, 0x0C,        //   Unit Exponent (-4)
0x66, 0x01, 0x10,  //   Unit (System: SI Linear, Time: Seconds)
0x47, 0xFF, 0xFF, 0x00, 0x00,  //   Physical Maximum (65534)
0x27, 0xFF, 0xFF, 0x00, 0x00,  //   Logical Maximum (65534)
0x75, 0x10,        //   Report Size (16)
0x95, 0x01,        //   Report Count (1)
0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x0D,        //   Usage Page (Digitizer)
0x09, 0x22,        //   Usage (Finger)
0xA1, 0x02,        //   Collection (Logical)
0x09, 0x47,        //     Usage (Confidence)
0x09, 0x42,        //     Usage (Tip Switch)
0x15, 0x00,        //     Logical Minimum (0)
0x25, 0x01,        //     Logical Maximum (1)
0x75, 0x01,        //     Report Size (1)
0x95, 0x02,        //     Report Count (2)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x75, 0x01,        //     Report Size (1)
0x95, 0x02,        //     Report Count (2)
0x81, 0x03,        //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x09, 0x51,        //     Usage (Contact ID)
0x25, 0x0F,        //     Logical Maximum (15)
0x75, 0x04,        //     Report Size (4)
0x95, 0x01,        //     Report Count (1)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
0x09, 0x30,        //     Usage (X)
0x15, 0x00,        //     Logical Minimum (0)
0x26, 0x12, 0x0E,  //     Logical Maximum (3602)
0x35, 0x00,        //     Physical Minimum (0)
0x46, 0xC2, 0x01,  //     Physical Maximum (450)
0x55, 0x0E,        //     Unit Exponent (-2)
0x65, 0x13,        //     Unit (System: English Linear, Length: Centimeter)
0x75, 0x10,        //     Report Size (16)
0x95, 0x01,        //     Report Count (1)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x09, 0x31,        //     Usage (Y)
0x26, 0xC3, 0x07,  //     Logical Maximum (1987)
0x46, 0xF8, 0x00,  //     Physical Maximum (248)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x0D,        //     Usage Page (Digitizer)
0x26, 0xFF, 0x0F,  //     Logical Maximum (4095)
0x75, 0x10,        //     Report Size (16)
0x09, 0x48,        //     Usage (Width)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x09, 0x49,        //     Usage (Height)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x75, 0x08,        //     Report Size (8)
0x25, 0xFF,        //     Logical Maximum (-1)
0x09, 0x30,        //     Usage (Tip Pressure)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0xC0,              //   End Collection
0x05, 0x0D,        //   Usage Page (Digitizer)
0x09, 0x22,        //   Usage (Finger)
0xA1, 0x02,        //   Collection (Logical)
0x09, 0x47,        //     Usage (Confidence)
0x09, 0x42,        //     Usage (Tip Switch)
0x15, 0x00,        //     Logical Minimum (0)
0x25, 0x01,        //     Logical Maximum (1)
0x75, 0x01,        //     Report Size (1)
0x95, 0x02,        //     Report Count (2)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x75, 0x01,        //     Report Size (1)
0x95, 0x02,        //     Report Count (2)
0x81, 0x03,        //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x09, 0x51,        //     Usage (Contact ID)
0x25, 0x0F,        //     Logical Maximum (15)
0x75, 0x04,        //     Report Size (4)
0x95, 0x01,        //     Report Count (1)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
0x09, 0x30,        //     Usage (X)
0x15, 0x00,        //     Logical Minimum (0)
0x26, 0x12, 0x0E,  //     Logical Maximum (3602)
0x35, 0x00,        //     Physical Minimum (0)
0x46, 0xC2, 0x01,  //     Physical Maximum (450)
0x55, 0x0E,        //     Unit Exponent (-2)
0x65, 0x13,        //     Unit (System: English Linear, Length: Centimeter)
0x75, 0x10,        //     Report Size (16)
0x95, 0x01,        //     Report Count (1)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x09, 0x31,        //     Usage (Y)
0x26, 0xC3, 0x07,  //     Logical Maximum (1987)
0x46, 0xF8, 0x00,  //     Physical Maximum (248)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x0D,        //     Usage Page (Digitizer)
0x26, 0xFF, 0x0F,  //     Logical Maximum (4095)
0x75, 0x10,        //     Report Size (16)
0x09, 0x48,        //     Usage (Width)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x09, 0x49,        //     Usage (Height)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x75, 0x08,        //     Report Size (8)
0x25, 0xFF,        //     Logical Maximum (-1)
0x09, 0x30,        //     Usage (Tip Pressure)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0xC0,              //   End Collection
0x05, 0x0D,        //   Usage Page (Digitizer)
0x09, 0x22,        //   Usage (Finger)
0xA1, 0x02,        //   Collection (Logical)
0x09, 0x47,        //     Usage (Confidence)
0x09, 0x42,        //     Usage (Tip Switch)
0x15, 0x00,        //     Logical Minimum (0)
0x25, 0x01,        //     Logical Maximum (1)
0x75, 0x01,        //     Report Size (1)
0x95, 0x02,        //     Report Count (2)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x75, 0x01,        //     Report Size (1)
0x95, 0x02,        //     Report Count (2)
0x81, 0x03,        //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x09, 0x51,        //     Usage (Contact ID)
0x25, 0x0F,        //     Logical Maximum (15)
0x75, 0x04,        //     Report Size (4)
0x95, 0x01,        //     Report Count (1)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
0x09, 0x30,        //     Usage (X)
0x15, 0x00,        //     Logical Minimum (0)
0x26, 0x12, 0x0E,  //     Logical Maximum (3602)
0x35, 0x00,        //     Physical Minimum (0)
0x46, 0xC2, 0x01,  //     Physical Maximum (450)
0x55, 0x0E,        //     Unit Exponent (-2)
0x65, 0x13,        //     Unit (System: English Linear, Length: Centimeter)
0x75, 0x10,        //     Report Size (16)
0x95, 0x01,        //     Report Count (1)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x09, 0x31,        //     Usage (Y)
0x26, 0xC3, 0x07,  //     Logical Maximum (1987)
0x46, 0xF8, 0x00,  //     Physical Maximum (248)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x0D,        //     Usage Page (Digitizer)
0x26, 0xFF, 0x0F,  //     Logical Maximum (4095)
0x75, 0x10,        //     Report Size (16)
0x09, 0x48,        //     Usage (Width)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x09, 0x49,        //     Usage (Length)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x75, 0x08,        //     Report Size (8)
0x25, 0xFF,        //     Logical Maximum (-1)
0x09, 0x30,        //     Usage (Tip Pressure)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0xC0,              //   End Collection
0x05, 0x0D,        //   Usage Page (Digitizer)
0x09, 0x22,        //   Usage (Finger)
0xA1, 0x02,        //   Collection (Logical)
0x09, 0x47,        //     Usage (Confidence)
0x09, 0x42,        //     Usage (Tip Switch)
0x15, 0x00,        //     Logical Minimum (0)
0x25, 0x01,        //     Logical Maximum (1)
0x75, 0x01,        //     Report Size (1)
0x95, 0x02,        //     Report Count (2)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x75, 0x01,        //     Report Size (1)
0x95, 0x02,        //     Report Count (2)
0x81, 0x03,        //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x09, 0x51,        //     Usage (Contact ID)
0x25, 0x0F,        //     Logical Maximum (15)
0x75, 0x04,        //     Report Size (4)
0x95, 0x01,        //     Report Count (1)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
0x09, 0x30,        //     Usage (X)
0x15, 0x00,        //     Logical Minimum (0)
0x26, 0x12, 0x0E,  //     Logical Maximum (3602)
0x35, 0x00,        //     Physical Minimum (0)
0x46, 0xC2, 0x01,  //     Physical Maximum (450)
0x55, 0x0E,        //     Unit Exponent (-2)
0x65, 0x13,        //     Unit (System: English Linear, Length: Centimeter)
0x75, 0x10,        //     Report Size (16)
0x95, 0x01,        //     Report Count (1)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x09, 0x31,        //     Usage (Y)
0x26, 0xC3, 0x07,  //     Logical Maximum (1987)
0x46, 0xF8, 0x00,  //     Physical Maximum (248)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x0D,        //     Usage Page (Digitizer)
0x26, 0xFF, 0x0F,  //     Logical Maximum (4095)
0x75, 0x10,        //     Report Size (16)
0x09, 0x48,        //     Usage (Width)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x09, 0x49,        //     Usage (Length)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x75, 0x08,        //     Report Size (8)
0x25, 0xFF,        //     Logical Maximum (-1)
0x09, 0x30,        //     Usage (Tip Pressure)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0xC0,              //   End Collection
0x05, 0x0D,        //   Usage Page (Digitizer)
0x09, 0x22,        //   Usage (Finger)
0xA1, 0x02,        //   Collection (Logical)
0x09, 0x47,        //     Usage (Confidence)
0x09, 0x42,        //     Usage (Tip Switch)
0x15, 0x00,        //     Logical Minimum (0)
0x25, 0x01,        //     Logical Maximum (1)
0x75, 0x01,        //     Report Size (1)
0x95, 0x02,        //     Report Count (2)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x75, 0x01,        //     Report Size (1)
0x95, 0x02,        //     Report Count (2)
0x81, 0x03,        //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x09, 0x51,        //     Usage (Contact ID)
0x25, 0x0F,        //     Logical Maximum (15)
0x75, 0x04,        //     Report Size (4)
0x95, 0x01,        //     Report Count (1)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
0x09, 0x30,        //     Usage (X)
0x15, 0x00,        //     Logical Minimum (0)
0x26, 0x12, 0x0E,  //     Logical Maximum (3602)
0x35, 0x00,        //     Physical Minimum (0)
0x46, 0xC2, 0x01,  //     Physical Maximum (450)
0x55, 0x0E,        //     Unit Exponent (-2)
0x65, 0x13,        //     Unit (System: English Linear, Length: Centimeter)
0x75, 0x10,        //     Report Size (16)
0x95, 0x01,        //     Report Count (1)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x09, 0x31,        //     Usage (Y)
0x26, 0xC3, 0x07,  //     Logical Maximum (1987)
0x46, 0xF8, 0x00,  //     Physical Maximum (248)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x0D,        //     Usage Page (Digitizer)
0x26, 0xFF, 0x0F,  //     Logical Maximum (4095)
0x75, 0x10,        //     Report Size (16)
0x09, 0x48,        //     Usage (Width)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x09, 0x49,        //     Usage (Length)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x75, 0x08,        //     Report Size (8)
0x25, 0xFF,        //     Logical Maximum (-1)
0x09, 0x30,        //     Usage (Tip Pressure)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0xC0,              //   End Collection
0x05, 0x0D,        //   Usage Page (Digitizer)
0x85, 0x02,        //   Report ID (2)
0x09, 0x55,        //   Usage (Count Count)
0x09, 0x59,        //   Usage (Pad Type)
0x75, 0x04,        //   Report Size (4)
0x95, 0x02,        //   Report Count (2)
0x25, 0x0F,        //   Logical Maximum (15)
0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
0x85, 0x07,        //   Report ID (7)
0x09, 0x60,        //   Usage (Latency Mode)
0x75, 0x01,        //   Report Size (1)
0x95, 0x01,        //   Report Count (1)
0x15, 0x00,        //   Logical Minimum (0)
0x25, 0x01,        //   Logical Maximum (1)
0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
0x95, 0x0F,        //   Report Count (15)
0xB1, 0x03,        //   Feature (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
0x85, 0x06,        //   Report ID (6)
0x09, 0xC5,        //   Usage (Device Certification Status)
0x15, 0x00,        //   Logical Minimum (0)
0x26, 0xFF, 0x00,  //   Logical Maximum (255)
0x75, 0x08,        //   Report Size (8)
0x96, 0x00, 0x01,  //   Report Count (256)
0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
0xC0,              // End Collection
0x05, 0x0D,        // Usage Page (Digitizer)
0x09, 0x0E,        // Usage (Device Configuration)
0xA1, 0x01,        // Collection (Application)
0x85, 0x03,        //   Report ID (3)
0x09, 0x22,        //   Usage (Finger)
0xA1, 0x00,        //   Collection (Physical)
0x09, 0x52,        //     Usage (Device Mode)
0x15, 0x00,        //     Logical Minimum (0)
0x25, 0x0A,        //     Logical Maximum (10)
0x75, 0x10,        //     Report Size (16)
0x95, 0x01,        //     Report Count (1)
0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
0xC0,              //   End Collection
0x09, 0x22,        //   Usage (Finger)
0xA1, 0x00,        //   Collection (Physical)
0x85, 0x05,        //     Report ID (5)
0x09, 0x57,        //     Usage (Surface Switch)
0x09, 0x58,        //     Usage (Button Switch)
0x75, 0x01,        //     Report Size (1)
0x95, 0x02,        //     Report Count (2)
0x25, 0x01,        //     Logical Maximum (1)
0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
0x95, 0x0E,        //     Report Count (14)
0xB1, 0x03,        //     Feature (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
0xC0,              //   End Collection
0xC0,              // End Collection

// 775 bytes
```

[set-feature-report-api]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.input.report/device.fidl?q=file:fuchsia.input.report%20file:fidl$%20SetFeatureReport&ss=fuchsia%2Ffuchsia
[touch-configuration-input-mode]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.input.report/touch.fidl?q=file:fuchsia.input.report%20%22type%20TouchConfigurationInputMode%22%20file:fidl&ss=fuchsia%2Ffuchsia
[hid-usage-tables-1-3]: https://usb.org/sites/default/files/hut1_3_0.pdf
[precision-touchpad-docs]: https://docs.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-windows-precision-touchpad-collection
[mouse-input-report-fidl]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.input.report/mouse.fidl?q=file:fuchsia.input.report%20%22type%20MouseInputReport%22
[touch-input-report-fidl]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.input.report/touch.fidl?q=file:fuchsia.input.report%20%22type%20TouchInputReport%22