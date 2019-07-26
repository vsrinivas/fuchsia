// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <hid-parser/units.h>

namespace {

constexpr uint32_t system_mask = 0xF;

// Taken from the Hid document on units. Each unit occupies
// a half byte (Nibble) so the shifts increment by 4 each time.
constexpr int system_shift = 0;
constexpr int length_shift = 4;
constexpr int mass_shift = 8;
constexpr int time_shift = 16;
constexpr int temperature_shift = 20;
constexpr int current_shift = 24;
constexpr int luminous_shift = 28;

constexpr int8_t SignExtendFromNibble(int8_t nibble) {
  // Expression taken and simplified from:
  // http://graphics.stanford.edu/~seander/bithacks.html#VariableSignExtend
  return static_cast<int8_t>(((nibble & 0x0F) ^ 0x08) - 0x08);
}

// Set the exponent of a given unit.
// Since exp will be converted to a nibble its values must range from [-8, 7].
constexpr void SetUnitTypeExp(uint32_t& type, int8_t exp, int unit_shift) {
  type = type | ((exp & 0x0F) << unit_shift);
}

// Get the exponent of a given unit.
// Exponents range from [-8, 7].
constexpr int8_t GetUnitTypeExp(uint32_t type, int unit_shift) {
  return SignExtendFromNibble(static_cast<int8_t>((type & (0xF << unit_shift)) >> unit_shift));
}

constexpr bool IsSiToEng(hid::unit::System system_in, hid::unit::System system_out) {
  return ((system_in == hid::unit::System::si_linear) ||
          (system_in == hid::unit::System::si_rotation)) &&
         ((system_out == hid::unit::System::eng_linear) ||
          (system_out == hid::unit::System::eng_rotation));
}

constexpr bool IsEngToSi(hid::unit::System system_in, hid::unit::System system_out) {
  return ((system_in == hid::unit::System::eng_linear) ||
          (system_in == hid::unit::System::eng_rotation)) &&
         ((system_out == hid::unit::System::si_linear) ||
          (system_out == hid::unit::System::si_rotation));
}

bool ConvertDistance(double val_in, const hid::Unit& unit_in, double& val_out,
                     const hid::Unit& unit_out) {
  int exp = hid::unit::GetLengthExp(unit_in);
  auto system_in = hid::unit::GetSystem(unit_in);
  auto system_out = hid::unit::GetSystem(unit_out);

  if ((exp == 0) || (system_in == system_out)) {
    val_out = val_in;
    return true;
  }

  // Centimeters to Inches.
  if ((system_in == hid::unit::System::si_linear) &&
      (system_out == hid::unit::System::eng_linear)) {
    val_out = val_in * pow(0.393701, exp);
    return true;
  }

  // Inches to Centimeters.
  if ((system_in == hid::unit::System::eng_linear) &&
      (system_out == hid::unit::System::si_linear)) {
    val_out = val_in * pow(2.54, exp);
    return true;
  }

  // Degrees to Radians.
  if ((system_in == hid::unit::System::eng_rotation) &&
      (system_out == hid::unit::System::si_rotation)) {
    val_out = val_in * pow((3.14159 / 180.0), exp);
    return true;
  }

  // Radians to Degrees.
  if ((system_in == hid::unit::System::si_rotation) &&
      (system_out == hid::unit::System::eng_rotation)) {
    val_out = val_in * pow((180.0 / 3.14159), exp);
    return true;
  }

  return false;
}

bool ConvertMass(double val_in, const hid::Unit& unit_in, double& val_out,
                 const hid::Unit& unit_out) {
  int exp = hid::unit::GetMassExp(unit_in);
  auto system_in = hid::unit::GetSystem(unit_in);
  auto system_out = hid::unit::GetSystem(unit_out);

  if ((exp == 0) || (system_in == system_out)) {
    val_out = val_in;
    return true;
  }

  // Grams to Slugs.
  if (IsSiToEng(system_in, system_out)) {
    val_out = val_in * pow(6.85218e-5, exp);
    return true;
  }

  // Slugs to Grams.
  if (IsEngToSi(system_in, system_out)) {
    val_out = val_in * pow(14593.9, exp);
    return true;
  }

  return false;
}

bool ConvertTemperature(double val_in, const hid::Unit& unit_in, double& val_out,
                        const hid::Unit& unit_out) {
  int exp = hid::unit::GetTemperatureExp(unit_in);
  auto system_in = hid::unit::GetSystem(unit_in);
  auto system_out = hid::unit::GetSystem(unit_out);

  if ((exp == 0) || (system_in == system_out)) {
    val_out = val_in;
    return true;
  }

  // Kelvin to Fahrenheit.
  if (IsSiToEng(system_in, system_out)) {
    val_out = (val_in - 273.15) * (9.0 / 5.0) + 32;
    return true;
  }

  // Fahrenheit to Kelvin.
  if (IsEngToSi(system_in, system_out)) {
    val_out = (val_in - 32) * (5.0 / 9.0) + 273.15;
    return true;
  }

  return false;
}

}  // namespace

namespace hid {
namespace unit {

void SetSystem(Unit& unit, hid::unit::System system) {
  SetUnitTypeExp(unit.type, static_cast<int8_t>(system), system_shift);
}

hid::unit::System GetSystem(const Unit& unit) {
  int sys = GetUnitTypeExp(unit.type, system_shift);
  switch (sys) {
    case 1:
      return hid::unit::System::si_linear;
    case 2:
      return hid::unit::System::si_rotation;
    case 3:
      return hid::unit::System::eng_linear;
    case 4:
      return hid::unit::System::eng_rotation;
    default:
      return hid::unit::System::reserved;
  }
}

void SetLengthExp(Unit& unit, int8_t exp) { SetUnitTypeExp(unit.type, exp, length_shift); }

void SetMassExp(Unit& unit, int8_t exp) { SetUnitTypeExp(unit.type, exp, mass_shift); }

void SetTimeExp(Unit& unit, int8_t exp) { SetUnitTypeExp(unit.type, exp, time_shift); }

void SetTemperatureExp(Unit& unit, int8_t exp) {
  SetUnitTypeExp(unit.type, exp, temperature_shift);
}

void SetCurrentExp(Unit& unit, int8_t exp) { SetUnitTypeExp(unit.type, exp, current_shift); }

void SetLuminousExp(Unit& unit, int8_t exp) { SetUnitTypeExp(unit.type, exp, luminous_shift); }

int GetLengthExp(const Unit& unit) { return GetUnitTypeExp(unit.type, length_shift); }

int GetMassExp(const Unit& unit) { return GetUnitTypeExp(unit.type, mass_shift); }

int GetTimeExp(const Unit& unit) { return GetUnitTypeExp(unit.type, time_shift); }

int GetTemperatureExp(const Unit& unit) { return GetUnitTypeExp(unit.type, temperature_shift); }

int GetCurrentExp(const Unit& unit) { return GetUnitTypeExp(unit.type, current_shift); }

int GetLuminousExp(const Unit& unit) { return GetUnitTypeExp(unit.type, luminous_shift); }

bool ConvertUnits(const Unit& unit_in, double val_in, const Unit& unit_out, double* val_out) {
  // If the units don't have the same measurements it's impossible to do a conversion.
  if ((unit_in.type & ~system_mask) != (unit_out.type & ~system_mask)) {
    return false;
  }

  double val = val_in * pow(10.0, unit_in.exp - unit_out.exp);

  if (unit_in.type == unit_out.type) {
    *val_out = val;
    return true;
  }

  if (!ConvertDistance(val, unit_in, val, unit_out)) {
    return false;
  }

  if (!ConvertMass(val, unit_in, val, unit_out)) {
    return false;
  }

  if (!ConvertTemperature(val, unit_in, val, unit_out)) {
    return false;
  }

  *val_out = val;
  return true;
}

}  // namespace unit
}  // namespace hid
