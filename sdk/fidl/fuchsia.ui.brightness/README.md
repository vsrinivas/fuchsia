# Brightness Component #

This component manages the screen backlight. It has two modes
auto-brightness and manual.

This interface will typically be used by the brightness UI but will
support concurrent users.

## Auto-brightness Mode ##
In this mode the brightness polls the light sensor and sets the
backlight as appropriate, darker as the sensor gets less light and
brighter as it gets more.

## Manual Mode ##
In manual mode the backlight remains at a fixed brightness until the
brightness component is told to set the backlight to another brightness
or auto-brightness mode is turned on.

## Notes ##
The brightness component always ensures that the screen backlight
changes smoothly over short period of time. This means that the
calling application can abruptly change the screen brightness
without having to worry about making the user jump!

The FIDL interface fuchsia.settings.display is used by the settings
component to tell it what to do. The settings component will use this
interface to controll brightness.

