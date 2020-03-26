# Battery Simulator

Battery Simulator is a simple command line interface for developers to use in testing behavior of their code with respect to different battery states independent (or in the absence) of a physical battery.


# Usage

Build and update your project on the target device (note that the product configuration must include //src/power:battery). Then run **fx shell** to enter the shell. To run the simulator, type **battery_cli**.

## Disconnect

Disconnects the BatteryManager from the physical battery, providing clients with simulated BatteryInfo data.
> battman> disconnect

## Reconnect

Reconnects the BatteryManager to the physical battery, providing clients with read BatteryInfo data.
> battman> reconnect

## Get

To get the current BatteryInfo, run
> battman> get

## Set

Modifies the state of the simulated battery. Must call 'disconnect' prior to 'set'. Arguments of the commands are separated by spaces.
> battman> set battery_attribute1 battery_attribute_value1 ... battery_attributeN battery_attribute_valueN

Where attribute is any of the following:

- BatteryPercentage
		- any int value ranging [1..100]
- ChargeSource
		- UNKNOWN
		- NONE
		- AC_ADAPTER
		- USB
		- WIRELESS
- LevelStatus
		- UNKNOWN
		- OK
		- WARNING
		- LOW
		- CRITICAL
- ChargeStatus
		- UNKNOWN
		- NOT_CHARGING
		- CHARGING
		- DISCHARGING
- BatteryStatus
		- UNKNOWN
		- OK
		- NOT_AVAILABLE
		- NOT_PRESENT
- TimeRemaining
        - Time represented in seconds

Some example commands
> battman> set BatteryPercentage 95
> battman> set LevelStatus LOW ChargeSource WIRELESS BatteryPercentage 12

