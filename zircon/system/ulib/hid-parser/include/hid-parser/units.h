// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HID_PARSER_UNITS_H_
#define HID_PARSER_UNITS_H_

#include <stdint.h>

#include <hid-parser/parser.h>

namespace hid {
namespace unit {

// UnitTypes are the helpful, "default" units in the system.
// These should used throughout the system.
enum class UnitType : uint32_t {
  // This is used when a HID device does not specify units.
  None,
  // This is used when a HID device has a set of units not described below.
  Other,
  // A measurement of distance in 10^-6 meter units.
  Distance,
  // A measurement of weight in 10^-3 gram units.
  Weight,
  // A measurement of rotation is 10^-3 degree.
  Rotation,
  // A measurement of angular velocity is 10^-3 deg/s.
  AngularVelocity,
  // A measurement of linear velocity is 10^-3 m/s
  LinearVelocity,
  // A measurement of acceleration is 10^-3 Gs
  Acceleration,
  // A measurement of magnetic_flux is 10^-6 Tesla (kg/(Amp * s^2))
  MagneticFlux,
  // A measurement of light is 1 Candela.
  Light,
  // A measurement of pressure is 10^-3 Pascal (kg/(m*s^2))
  Pressure,
  // A measurement of lux is 10^-6 Candela/(m^2)
  Lux,
};

// Get the exact unit from the UnitType.
Unit GetUnitFromUnitType(UnitType type);

// Get the closest convertible UnitType from the unit.
// If the unit cannot be converted into a UnitType, |Other| will be returned.
// If there are no unit's, |None| will be returned.
UnitType GetUnitTypeFromUnit(const Unit& unit);

double ConvertValToUnitType(const Unit& unit_in, double val_in);

// Below is the full unit definitions as outlined by the HID standard.
// These can be used if there is a strong need to do something more
// specific than using a UnitType.

// Each system defines the units for the following measurements:
// length, mass, time, temperature, current, luminous intensity.
enum class System : int8_t {
  // SI Linear has the following units:
  // centimeter, gram, seconds, kelvin, ampere, candela.
  si_linear = 0x1,
  // SI Rotation has the following units:
  // radians, gram, seconds, kelvin, ampere, candela.
  si_rotation = 0x2,
  // English Linear has the following units:
  // inch, slug, seconds, fahrenheit, ampere, candela.
  eng_linear = 0x3,
  // English Rotation has the following units:
  // degrees, slug, seconds, fahrenheit, ampere, candela.
  eng_rotation = 0x4,
  reserved = 0x5,
};

// Sets a Unit's system. A unit can only belong to a single system.
// Calling SetSystem on a Unit that already has a system defined
// will overwrite the current system.
void SetSystem(Unit& unit, hid::unit::System system);
hid::unit::System GetSystem(const Unit& unit);

// The functions below set the exponent for various measurements.
// exp must be within [-8, 7].
// The measurement's unit is defined by the Unit's system.
// Example: Momentum is (mass * distance / time) so it has a mass
//          exponent of 1, a distance exponent of 1, and a time
//          exponent of -1. Under the SI Linear system this would
//          be (gram * centimeter / seconds).
void SetLengthExp(Unit& unit, int8_t exp);
void SetMassExp(Unit& unit, int8_t exp);
void SetTimeExp(Unit& unit, int8_t exp);
void SetTemperatureExp(Unit& unit, int8_t exp);
void SetCurrentExp(Unit& unit, int8_t exp);
void SetLuminousExp(Unit& unit, int8_t exp);

// The functions below get the exponent for various measurements.
// The return value will be within [-8, 7].
int GetLengthExp(const Unit& unit);
int GetMassExp(const Unit& unit);
int GetTimeExp(const Unit& unit);
int GetTemperatureExp(const Unit& unit);
int GetCurrentExp(const Unit& unit);
int GetLuminousExp(const Unit& unit);

// Convert a value from one unit to another.
// Returns False if it is impossible to do the conversion.
bool ConvertUnits(const Unit& unit_in, double val_in, const Unit& unit_out, double* val_out);

}  // namespace unit
}  // namespace hid

#endif  // HID_PARSER_UNITS_H_
