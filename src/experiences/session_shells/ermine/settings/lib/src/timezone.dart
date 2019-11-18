// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:flutter/material.dart';

import 'package:fidl_fuchsia_deprecatedtimezone/fidl_async.dart';
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart' show StartupContext;
import 'package:internationalization/strings.dart';
import 'package:quickui/quickui.dart';

/// Defines a [UiSpec] for displaying and changing timezone.
class TimeZone extends UiSpec {
  // Localized strings.
  static String get _title => Strings.timezone;

  // Action to change timezone.
  static int changeAction = QuickAction.details.$value;

  _TimeZoneModel model;

  TimeZone({TimezoneProxy timezone, TimezoneWatcherBinding binding}) {
    model = _TimeZoneModel(
      timezone: timezone,
      binding: binding,
      onChange: _onChange,
    );
  }

  factory TimeZone.fromStartupContext(StartupContext startupContext) {
    // Connect to timeservice and update the system time.
    final timeService = TimeServiceProxy();
    startupContext.incoming.connectToService(timeService);
    timeService.update(3 /* num_retries */).then((success) {
      if (!success) {
        log.warning('Failed to update system time from the network.');
      }
      timeService.ctrl.close();
    });

    // Connect to timezone service.
    final timezoneService = TimezoneProxy();
    startupContext.incoming.connectToService(timezoneService);

    final timezone = TimeZone(timezone: timezoneService);
    return timezone;
  }

  void _onChange() async {
    spec = _specForTimeZone(model);
  }

  @override
  void update(Value value) async {
    if (value.$tag == ValueTag.button &&
        value.button.action == QuickAction.cancel.$value) {
      spec = _specForTimeZone(model);
    } else if (value.$tag == ValueTag.text && value.text.action > 0) {
      if (value.text.action == changeAction) {
        spec = _specForTimeZone(model, changeAction);
      } else {
        final index = value.text.action ^ QuickAction.submit.$value;
        model.timezoneId = _kTimeZones[index].zoneId;
        spec = _specForTimeZone(model);
      }
    }
  }

  @override
  void dispose() {
    model.dispose();
  }

  static Spec _specForTimeZone(_TimeZoneModel model, [int action = 0]) {
    if (action == 0 || action & QuickAction.cancel.$value > 0) {
      return Spec(title: _title, groups: [
        Group(title: _title, values: [
          Value.withText(TextValue(
            text: model.timezoneId,
            action: changeAction,
          )),
        ]),
      ]);
    } else if (action == changeAction) {
      final values = List<TextValue>.generate(
          _kTimeZones.length,
          (index) => TextValue(
                text: _kTimeZones[index].zoneId,
                action: QuickAction.submit.$value | index,
              ));
      return Spec(title: _title, groups: [
        Group(title: 'Select Timezone', values: [
          Value.withGrid(GridValue(
            columns: 1,
            values: values,
          )),
          Value.withButton(ButtonValue(
            label: 'close',
            action: QuickAction.cancel.$value,
          )),
        ]),
      ]);
    } else {
      return null;
    }
  }
}

class _TimeZoneModel {
  final TimezoneProxy timezone;
  final TimezoneWatcherBinding _binding;
  final VoidCallback onChange;

  String _timezoneId;

  _TimeZoneModel({this.timezone, TimezoneWatcherBinding binding, this.onChange})
      : _binding = binding ?? TimezoneWatcherBinding() {
    // Get current timezone and watch it for changes.
    timezone
      ..getTimezoneId().then((tz) {
        _timezoneId = tz;
        onChange();
      })
      ..watch(_binding.wrap(_TimezoneWatcherImpl(this)));
  }

  void dispose() {
    timezone.ctrl.close();
    _binding.close();
  }

  String get timezoneId => _timezoneId;
  set timezoneId(String value) {
    _timezoneId = value;
    timezone.setTimezone(value);
  }
}

class _TimezoneWatcherImpl extends TimezoneWatcher {
  final _TimeZoneModel model;
  _TimezoneWatcherImpl(this.model);
  @override
  Future<void> onTimezoneOffsetChange(String timezoneId) async {
    model._timezoneId = timezoneId;
    model.onChange();
  }
}

class _Timezone {
  /// The ICU standard zone ID.
  final String zoneId;

  const _Timezone({this.zoneId});
}

// Note: these timezones were generated from a script using ICU data.
// These should ideally be loaded ad hoc or stored somewhere.
const List<_Timezone> _kTimeZones = <_Timezone>[
  _Timezone(zoneId: 'US/Eastern'),
  _Timezone(zoneId: 'US/Pacific'),
  _Timezone(zoneId: 'Europe/Paris'),
  _Timezone(zoneId: 'Africa/Abidjan'),
  _Timezone(zoneId: 'Africa/Accra'),
  _Timezone(zoneId: 'Africa/Addis_Ababa'),
  _Timezone(zoneId: 'Africa/Algiers'),
  _Timezone(zoneId: 'Africa/Asmara'),
  _Timezone(zoneId: 'Africa/Asmera'),
  _Timezone(zoneId: 'Africa/Bamako'),
  _Timezone(zoneId: 'Africa/Bangui'),
  _Timezone(zoneId: 'Africa/Banjul'),
  _Timezone(zoneId: 'Africa/Bissau'),
  _Timezone(zoneId: 'Africa/Blantyre'),
  _Timezone(zoneId: 'Africa/Brazzaville'),
  _Timezone(zoneId: 'Africa/Bujumbura'),
  _Timezone(zoneId: 'Africa/Cairo'),
  _Timezone(zoneId: 'Africa/Casablanca'),
  _Timezone(zoneId: 'Africa/Ceuta'),
  _Timezone(zoneId: 'Africa/Conakry'),
  _Timezone(zoneId: 'Africa/Dakar'),
  _Timezone(zoneId: 'Africa/Dar_es_Salaam'),
  _Timezone(zoneId: 'Africa/Djibouti'),
  _Timezone(zoneId: 'Africa/Douala'),
  _Timezone(zoneId: 'Africa/El_Aaiun'),
  _Timezone(zoneId: 'Africa/Freetown'),
  _Timezone(zoneId: 'Africa/Gaborone'),
  _Timezone(zoneId: 'Africa/Harare'),
  _Timezone(zoneId: 'Africa/Johannesburg'),
  _Timezone(zoneId: 'Africa/Kampala'),
  _Timezone(zoneId: 'Africa/Khartoum'),
  _Timezone(zoneId: 'Africa/Kigali'),
  _Timezone(zoneId: 'Africa/Kinshasa'),
  _Timezone(zoneId: 'Africa/Lagos'),
  _Timezone(zoneId: 'Africa/Libreville'),
  _Timezone(zoneId: 'Africa/Lome'),
  _Timezone(zoneId: 'Africa/Luanda'),
  _Timezone(zoneId: 'Africa/Lubumbashi'),
  _Timezone(zoneId: 'Africa/Lusaka'),
  _Timezone(zoneId: 'Africa/Malabo'),
  _Timezone(zoneId: 'Africa/Maputo'),
  _Timezone(zoneId: 'Africa/Maseru'),
  _Timezone(zoneId: 'Africa/Mbabane'),
  _Timezone(zoneId: 'Africa/Mogadishu'),
  _Timezone(zoneId: 'Africa/Monrovia'),
  _Timezone(zoneId: 'Africa/Nairobi'),
  _Timezone(zoneId: 'Africa/Ndjamena'),
  _Timezone(zoneId: 'Africa/Niamey'),
  _Timezone(zoneId: 'Africa/Nouakchott'),
  _Timezone(zoneId: 'Africa/Ouagadougou'),
  _Timezone(zoneId: 'Africa/Porto-Novo'),
  _Timezone(zoneId: 'Africa/Sao_Tome'),
  _Timezone(zoneId: 'Africa/Timbuktu'),
  _Timezone(zoneId: 'Africa/Tripoli'),
  _Timezone(zoneId: 'Africa/Tunis'),
  _Timezone(zoneId: 'Africa/Windhoek'),
  _Timezone(zoneId: 'America/Adak'),
  _Timezone(zoneId: 'America/Anchorage'),
  _Timezone(zoneId: 'America/Anguilla'),
  _Timezone(zoneId: 'America/Antigua'),
  _Timezone(zoneId: 'America/Araguaina'),
  _Timezone(zoneId: 'America/Argentina/Buenos_Aires'),
  _Timezone(zoneId: 'America/Argentina/Catamarca'),
  _Timezone(zoneId: 'America/Argentina/ComodRivadavia'),
  _Timezone(zoneId: 'America/Argentina/Cordoba'),
  _Timezone(zoneId: 'America/Argentina/Jujuy'),
  _Timezone(zoneId: 'America/Argentina/La_Rioja'),
  _Timezone(zoneId: 'America/Argentina/Mendoza'),
  _Timezone(zoneId: 'America/Argentina/Rio_Gallegos'),
  _Timezone(zoneId: 'America/Argentina/San_Juan'),
  _Timezone(zoneId: 'America/Argentina/Tucuman'),
  _Timezone(zoneId: 'America/Argentina/Ushuaia'),
  _Timezone(zoneId: 'America/Aruba'),
  _Timezone(zoneId: 'America/Asuncion'),
  _Timezone(zoneId: 'America/Atikokan'),
  _Timezone(zoneId: 'America/Atka'),
  _Timezone(zoneId: 'America/Bahia'),
  _Timezone(zoneId: 'America/Barbados'),
  _Timezone(zoneId: 'America/Belem'),
  _Timezone(zoneId: 'America/Belize'),
  _Timezone(zoneId: 'America/Blanc-Sablon'),
  _Timezone(zoneId: 'America/Boa_Vista'),
  _Timezone(zoneId: 'America/Bogota'),
  _Timezone(zoneId: 'America/Boise'),
  _Timezone(zoneId: 'America/Buenos_Aires'),
  _Timezone(zoneId: 'America/Cambridge_Bay'),
  _Timezone(zoneId: 'America/Campo_Grande'),
  _Timezone(zoneId: 'America/Cancun'),
  _Timezone(zoneId: 'America/Caracas'),
  _Timezone(zoneId: 'America/Catamarca'),
  _Timezone(zoneId: 'America/Cayenne'),
  _Timezone(zoneId: 'America/Cayman'),
  _Timezone(zoneId: 'America/Chicago'),
  _Timezone(zoneId: 'America/Chihuahua'),
  _Timezone(zoneId: 'America/Coral_Harbour'),
  _Timezone(zoneId: 'America/Cordoba'),
  _Timezone(zoneId: 'America/Costa_Rica'),
  _Timezone(zoneId: 'America/Cuiaba'),
  _Timezone(zoneId: 'America/Curacao'),
  _Timezone(zoneId: 'America/Danmarkshavn'),
  _Timezone(zoneId: 'America/Dawson'),
  _Timezone(zoneId: 'America/Dawson_Creek'),
  _Timezone(zoneId: 'America/Denver'),
  _Timezone(zoneId: 'America/Detroit'),
  _Timezone(zoneId: 'America/Dominica'),
  _Timezone(zoneId: 'America/Edmonton'),
  _Timezone(zoneId: 'America/Eirunepe'),
  _Timezone(zoneId: 'America/El_Salvador'),
  _Timezone(zoneId: 'America/Ensenada'),
  _Timezone(zoneId: 'America/Fort_Wayne'),
  _Timezone(zoneId: 'America/Fortaleza'),
  _Timezone(zoneId: 'America/Glace_Bay'),
  _Timezone(zoneId: 'America/Godthab'),
  _Timezone(zoneId: 'America/Goose_Bay'),
  _Timezone(zoneId: 'America/Grand_Turk'),
  _Timezone(zoneId: 'America/Grenada'),
  _Timezone(zoneId: 'America/Guadeloupe'),
  _Timezone(zoneId: 'America/Guatemala'),
  _Timezone(zoneId: 'America/Guayaquil'),
  _Timezone(zoneId: 'America/Guyana'),
  _Timezone(zoneId: 'America/Halifax'),
  _Timezone(zoneId: 'America/Havana'),
  _Timezone(zoneId: 'America/Hermosillo'),
  _Timezone(zoneId: 'America/Indiana/Indianapolis'),
  _Timezone(zoneId: 'America/Indiana/Knox'),
  _Timezone(zoneId: 'America/Indiana/Marengo'),
  _Timezone(zoneId: 'America/Indiana/Petersburg'),
  _Timezone(zoneId: 'America/Indiana/Tell_City'),
  _Timezone(zoneId: 'America/Indiana/Vevay'),
  _Timezone(zoneId: 'America/Indiana/Vincennes'),
  _Timezone(zoneId: 'America/Indiana/Winamac'),
  _Timezone(zoneId: 'America/Indianapolis'),
  _Timezone(zoneId: 'America/Inuvik'),
  _Timezone(zoneId: 'America/Iqaluit'),
  _Timezone(zoneId: 'America/Jamaica'),
  _Timezone(zoneId: 'America/Jujuy'),
  _Timezone(zoneId: 'America/Juneau'),
  _Timezone(zoneId: 'America/Kentucky/Louisville'),
  _Timezone(zoneId: 'America/Kentucky/Monticello'),
  _Timezone(zoneId: 'America/Knox_IN'),
  _Timezone(zoneId: 'America/La_Paz'),
  _Timezone(zoneId: 'America/Lima'),
  _Timezone(zoneId: 'America/Los_Angeles'),
  _Timezone(zoneId: 'America/Louisville'),
  _Timezone(zoneId: 'America/Maceio'),
  _Timezone(zoneId: 'America/Managua'),
  _Timezone(zoneId: 'America/Manaus'),
  _Timezone(zoneId: 'America/Marigot'),
  _Timezone(zoneId: 'America/Martinique'),
  _Timezone(zoneId: 'America/Mazatlan'),
  _Timezone(zoneId: 'America/Mendoza'),
  _Timezone(zoneId: 'America/Menominee'),
  _Timezone(zoneId: 'America/Merida'),
  _Timezone(zoneId: 'America/Mexico_City'),
  _Timezone(zoneId: 'America/Miquelon'),
  _Timezone(zoneId: 'America/Moncton'),
  _Timezone(zoneId: 'America/Monterrey'),
  _Timezone(zoneId: 'America/Montevideo'),
  _Timezone(zoneId: 'America/Montreal'),
  _Timezone(zoneId: 'America/Montserrat'),
  _Timezone(zoneId: 'America/Nassau'),
  _Timezone(zoneId: 'America/New_York'),
  _Timezone(zoneId: 'America/Nipigon'),
  _Timezone(zoneId: 'America/Nome'),
  _Timezone(zoneId: 'America/Noronha'),
  _Timezone(zoneId: 'America/North_Dakota/Center'),
  _Timezone(zoneId: 'America/North_Dakota/New_Salem'),
  _Timezone(zoneId: 'America/Panama'),
  _Timezone(zoneId: 'America/Pangnirtung'),
  _Timezone(zoneId: 'America/Paramaribo'),
  _Timezone(zoneId: 'America/Phoenix'),
  _Timezone(zoneId: 'America/Port-au-Prince'),
  _Timezone(zoneId: 'America/Port_of_Spain'),
  _Timezone(zoneId: 'America/Porto_Acre'),
  _Timezone(zoneId: 'America/Porto_Velho'),
  _Timezone(zoneId: 'America/Puerto_Rico'),
  _Timezone(zoneId: 'America/Rainy_River'),
  _Timezone(zoneId: 'America/Rankin_Inlet'),
  _Timezone(zoneId: 'America/Recife'),
  _Timezone(zoneId: 'America/Regina'),
  _Timezone(zoneId: 'America/Resolute'),
  _Timezone(zoneId: 'America/Rio_Branco'),
  _Timezone(zoneId: 'America/Rosario'),
  _Timezone(zoneId: 'America/Santiago'),
  _Timezone(zoneId: 'America/Santo_Domingo'),
  _Timezone(zoneId: 'America/Sao_Paulo'),
  _Timezone(zoneId: 'America/Scoresbysund'),
  _Timezone(zoneId: 'America/Shiprock'),
  _Timezone(zoneId: 'America/St_Barthelemy'),
  _Timezone(zoneId: 'America/St_Johns'),
  _Timezone(zoneId: 'America/St_Kitts'),
  _Timezone(zoneId: 'America/St_Lucia'),
  _Timezone(zoneId: 'America/St_Thomas'),
  _Timezone(zoneId: 'America/St_Vincent'),
  _Timezone(zoneId: 'America/Swift_Current'),
  _Timezone(zoneId: 'America/Tegucigalpa'),
  _Timezone(zoneId: 'America/Thule'),
  _Timezone(zoneId: 'America/Thunder_Bay'),
  _Timezone(zoneId: 'America/Tijuana'),
  _Timezone(zoneId: 'America/Toronto'),
  _Timezone(zoneId: 'America/Tortola'),
  _Timezone(zoneId: 'America/Vancouver'),
  _Timezone(zoneId: 'America/Virgin'),
  _Timezone(zoneId: 'America/Whitehorse'),
  _Timezone(zoneId: 'America/Winnipeg'),
  _Timezone(zoneId: 'America/Yakutat'),
  _Timezone(zoneId: 'America/Yellowknife'),
  _Timezone(zoneId: 'Antarctica/Casey'),
  _Timezone(zoneId: 'Antarctica/Davis'),
  _Timezone(zoneId: 'Antarctica/DumontDUrville'),
  _Timezone(zoneId: 'Antarctica/Mawson'),
  _Timezone(zoneId: 'Antarctica/McMurdo'),
  _Timezone(zoneId: 'Antarctica/Palmer'),
  _Timezone(zoneId: 'Antarctica/Rothera'),
  _Timezone(zoneId: 'Antarctica/South_Pole'),
  _Timezone(zoneId: 'Antarctica/Syowa'),
  _Timezone(zoneId: 'Antarctica/Vostok'),
  _Timezone(zoneId: 'Arctic/Longyearbyen'),
  _Timezone(zoneId: 'Asia/Aden'),
  _Timezone(zoneId: 'Asia/Almaty'),
  _Timezone(zoneId: 'Asia/Amman'),
  _Timezone(zoneId: 'Asia/Anadyr'),
  _Timezone(zoneId: 'Asia/Aqtau'),
  _Timezone(zoneId: 'Asia/Aqtobe'),
  _Timezone(zoneId: 'Asia/Ashgabat'),
  _Timezone(zoneId: 'Asia/Ashkhabad'),
  _Timezone(zoneId: 'Asia/Baghdad'),
  _Timezone(zoneId: 'Asia/Bahrain'),
  _Timezone(zoneId: 'Asia/Baku'),
  _Timezone(zoneId: 'Asia/Bangkok'),
  _Timezone(zoneId: 'Asia/Beirut'),
  _Timezone(zoneId: 'Asia/Bishkek'),
  _Timezone(zoneId: 'Asia/Brunei'),
  _Timezone(zoneId: 'Asia/Calcutta'),
  _Timezone(zoneId: 'Asia/Choibalsan'),
  _Timezone(zoneId: 'Asia/Chongqing'),
  _Timezone(zoneId: 'Asia/Chungking'),
  _Timezone(zoneId: 'Asia/Colombo'),
  _Timezone(zoneId: 'Asia/Dacca'),
  _Timezone(zoneId: 'Asia/Damascus'),
  _Timezone(zoneId: 'Asia/Dhaka'),
  _Timezone(zoneId: 'Asia/Dili'),
  _Timezone(zoneId: 'Asia/Dubai'),
  _Timezone(zoneId: 'Asia/Dushanbe'),
  _Timezone(zoneId: 'Asia/Gaza'),
  _Timezone(zoneId: 'Asia/Harbin'),
  _Timezone(zoneId: 'Asia/Hong_Kong'),
  _Timezone(zoneId: 'Asia/Hovd'),
  _Timezone(zoneId: 'Asia/Irkutsk'),
  _Timezone(zoneId: 'Asia/Istanbul'),
  _Timezone(zoneId: 'Asia/Jakarta'),
  _Timezone(zoneId: 'Asia/Jayapura'),
  _Timezone(zoneId: 'Asia/Jerusalem'),
  _Timezone(zoneId: 'Asia/Kabul'),
  _Timezone(zoneId: 'Asia/Kamchatka'),
  _Timezone(zoneId: 'Asia/Karachi'),
  _Timezone(zoneId: 'Asia/Kashgar'),
  _Timezone(zoneId: 'Asia/Katmandu'),
  _Timezone(zoneId: 'Asia/Krasnoyarsk'),
  _Timezone(zoneId: 'Asia/Kuala_Lumpur'),
  _Timezone(zoneId: 'Asia/Kuching'),
  _Timezone(zoneId: 'Asia/Kuwait'),
  _Timezone(zoneId: 'Asia/Macao'),
  _Timezone(zoneId: 'Asia/Macau'),
  _Timezone(zoneId: 'Asia/Magadan'),
  _Timezone(zoneId: 'Asia/Makassar'),
  _Timezone(zoneId: 'Asia/Manila'),
  _Timezone(zoneId: 'Asia/Muscat'),
  _Timezone(zoneId: 'Asia/Nicosia'),
  _Timezone(zoneId: 'Asia/Novosibirsk'),
  _Timezone(zoneId: 'Asia/Omsk'),
  _Timezone(zoneId: 'Asia/Oral'),
  _Timezone(zoneId: 'Asia/Phnom_Penh'),
  _Timezone(zoneId: 'Asia/Pontianak'),
  _Timezone(zoneId: 'Asia/Pyongyang'),
  _Timezone(zoneId: 'Asia/Qatar'),
  _Timezone(zoneId: 'Asia/Qyzylorda'),
  _Timezone(zoneId: 'Asia/Rangoon'),
  _Timezone(zoneId: 'Asia/Riyadh'),
  _Timezone(zoneId: 'Asia/Riyadh87'),
  _Timezone(zoneId: 'Asia/Riyadh88'),
  _Timezone(zoneId: 'Asia/Riyadh89'),
  _Timezone(zoneId: 'Asia/Saigon'),
  _Timezone(zoneId: 'Asia/Sakhalin'),
  _Timezone(zoneId: 'Asia/Samarkand'),
  _Timezone(zoneId: 'Asia/Seoul'),
  _Timezone(zoneId: 'Asia/Shanghai'),
  _Timezone(zoneId: 'Asia/Singapore'),
  _Timezone(zoneId: 'Asia/Taipei'),
  _Timezone(zoneId: 'Asia/Tashkent'),
  _Timezone(zoneId: 'Asia/Tbilisi'),
  _Timezone(zoneId: 'Asia/Tehran'),
  _Timezone(zoneId: 'Asia/Tel_Aviv'),
  _Timezone(zoneId: 'Asia/Thimbu'),
  _Timezone(zoneId: 'Asia/Thimphu'),
  _Timezone(zoneId: 'Asia/Tokyo'),
  _Timezone(zoneId: 'Asia/Ujung_Pandang'),
  _Timezone(zoneId: 'Asia/Ulaanbaatar'),
  _Timezone(zoneId: 'Asia/Ulan_Bator'),
  _Timezone(zoneId: 'Asia/Urumqi'),
  _Timezone(zoneId: 'Asia/Vientiane'),
  _Timezone(zoneId: 'Asia/Vladivostok'),
  _Timezone(zoneId: 'Asia/Yakutsk'),
  _Timezone(zoneId: 'Asia/Yekaterinburg'),
  _Timezone(zoneId: 'Asia/Yerevan'),
  _Timezone(zoneId: 'Atlantic/Azores'),
  _Timezone(zoneId: 'Atlantic/Bermuda'),
  _Timezone(zoneId: 'Atlantic/Canary'),
  _Timezone(zoneId: 'Atlantic/Cape_Verde'),
  _Timezone(zoneId: 'Atlantic/Faeroe'),
  _Timezone(zoneId: 'Atlantic/Faroe'),
  _Timezone(zoneId: 'Atlantic/Jan_Mayen'),
  _Timezone(zoneId: 'Atlantic/Madeira'),
  _Timezone(zoneId: 'Atlantic/Reykjavik'),
  _Timezone(zoneId: 'Atlantic/South_Georgia'),
  _Timezone(zoneId: 'Atlantic/St_Helena'),
  _Timezone(zoneId: 'Atlantic/Stanley'),
  _Timezone(zoneId: 'Australia/ACT'),
  _Timezone(zoneId: 'Australia/Adelaide'),
  _Timezone(zoneId: 'Australia/Brisbane'),
  _Timezone(zoneId: 'Australia/Broken_Hill'),
  _Timezone(zoneId: 'Australia/Canberra'),
  _Timezone(zoneId: 'Australia/Currie'),
  _Timezone(zoneId: 'Australia/Darwin'),
  _Timezone(zoneId: 'Australia/Eucla'),
  _Timezone(zoneId: 'Australia/Hobart'),
  _Timezone(zoneId: 'Australia/LHI'),
  _Timezone(zoneId: 'Australia/Lindeman'),
  _Timezone(zoneId: 'Australia/Lord_Howe'),
  _Timezone(zoneId: 'Australia/Melbourne'),
  _Timezone(zoneId: 'Australia/NSW'),
  _Timezone(zoneId: 'Australia/North'),
  _Timezone(zoneId: 'Australia/Perth'),
  _Timezone(zoneId: 'Australia/Queensland'),
  _Timezone(zoneId: 'Australia/South'),
  _Timezone(zoneId: 'Australia/Sydney'),
  _Timezone(zoneId: 'Australia/Tasmania'),
  _Timezone(zoneId: 'Australia/Victoria'),
  _Timezone(zoneId: 'Australia/West'),
  _Timezone(zoneId: 'Australia/Yancowinna'),
  _Timezone(zoneId: 'Brazil/Acre'),
  _Timezone(zoneId: 'Brazil/DeNoronha'),
  _Timezone(zoneId: 'Brazil/East'),
  _Timezone(zoneId: 'Brazil/West'),
  _Timezone(zoneId: 'CET'),
  _Timezone(zoneId: 'CST6CDT'),
  _Timezone(zoneId: 'Canada/Atlantic'),
  _Timezone(zoneId: 'Canada/Central'),
  _Timezone(zoneId: 'Canada/East-Saskatchewan'),
  _Timezone(zoneId: 'Canada/Eastern'),
  _Timezone(zoneId: 'Canada/Mountain'),
  _Timezone(zoneId: 'Canada/Newfoundland'),
  _Timezone(zoneId: 'Canada/Pacific'),
  _Timezone(zoneId: 'Canada/Saskatchewan'),
  _Timezone(zoneId: 'Canada/Yukon'),
  _Timezone(zoneId: 'Chile/Continental'),
  _Timezone(zoneId: 'Chile/EasterIsland'),
  _Timezone(zoneId: 'Cuba'),
  _Timezone(zoneId: 'EET'),
  _Timezone(zoneId: 'EST'),
  _Timezone(zoneId: 'EST5EDT'),
  _Timezone(zoneId: 'Egypt'),
  _Timezone(zoneId: 'Eire'),
  _Timezone(zoneId: 'Etc/GMT'),
  _Timezone(zoneId: 'Etc/GMT+0'),
  _Timezone(zoneId: 'Etc/GMT+1'),
  _Timezone(zoneId: 'Etc/GMT+10'),
  _Timezone(zoneId: 'Etc/GMT+11'),
  _Timezone(zoneId: 'Etc/GMT+12'),
  _Timezone(zoneId: 'Etc/GMT+2'),
  _Timezone(zoneId: 'Etc/GMT+3'),
  _Timezone(zoneId: 'Etc/GMT+4'),
  _Timezone(zoneId: 'Etc/GMT+5'),
  _Timezone(zoneId: 'Etc/GMT+6'),
  _Timezone(zoneId: 'Etc/GMT+7'),
  _Timezone(zoneId: 'Etc/GMT+8'),
  _Timezone(zoneId: 'Etc/GMT+9'),
  _Timezone(zoneId: 'Etc/GMT-0'),
  _Timezone(zoneId: 'Etc/GMT-1'),
  _Timezone(zoneId: 'Etc/GMT-10'),
  _Timezone(zoneId: 'Etc/GMT-11'),
  _Timezone(zoneId: 'Etc/GMT-12'),
  _Timezone(zoneId: 'Etc/GMT-13'),
  _Timezone(zoneId: 'Etc/GMT-14'),
  _Timezone(zoneId: 'Etc/GMT-2'),
  _Timezone(zoneId: 'Etc/GMT-3'),
  _Timezone(zoneId: 'Etc/GMT-4'),
  _Timezone(zoneId: 'Etc/GMT-5'),
  _Timezone(zoneId: 'Etc/GMT-6'),
  _Timezone(zoneId: 'Etc/GMT-7'),
  _Timezone(zoneId: 'Etc/GMT-8'),
  _Timezone(zoneId: 'Etc/GMT-9'),
  _Timezone(zoneId: 'Etc/GMT0'),
  _Timezone(zoneId: 'Etc/Greenwich'),
  _Timezone(zoneId: 'Etc/UCT'),
  _Timezone(zoneId: 'Etc/UTC'),
  _Timezone(zoneId: 'Etc/Universal'),
  _Timezone(zoneId: 'Etc/Zulu'),
  _Timezone(zoneId: 'Europe/Amsterdam'),
  _Timezone(zoneId: 'Europe/Andorra'),
  _Timezone(zoneId: 'Europe/Athens'),
  _Timezone(zoneId: 'Europe/Belfast'),
  _Timezone(zoneId: 'Europe/Belgrade'),
  _Timezone(zoneId: 'Europe/Berlin'),
  _Timezone(zoneId: 'Europe/Bratislava'),
  _Timezone(zoneId: 'Europe/Brussels'),
  _Timezone(zoneId: 'Europe/Bucharest'),
  _Timezone(zoneId: 'Europe/Budapest'),
  _Timezone(zoneId: 'Europe/Chisinau'),
  _Timezone(zoneId: 'Europe/Copenhagen'),
  _Timezone(zoneId: 'Europe/Dublin'),
  _Timezone(zoneId: 'Europe/Gibraltar'),
  _Timezone(zoneId: 'Europe/Guernsey'),
  _Timezone(zoneId: 'Europe/Helsinki'),
  _Timezone(zoneId: 'Europe/Isle_of_Man'),
  _Timezone(zoneId: 'Europe/Istanbul'),
  _Timezone(zoneId: 'Europe/Jersey'),
  _Timezone(zoneId: 'Europe/Kaliningrad'),
  _Timezone(zoneId: 'Europe/Kiev'),
  _Timezone(zoneId: 'Europe/Lisbon'),
  _Timezone(zoneId: 'Europe/Ljubljana'),
  _Timezone(zoneId: 'Europe/London'),
  _Timezone(zoneId: 'Europe/Luxembourg'),
  _Timezone(zoneId: 'Europe/Madrid'),
  _Timezone(zoneId: 'Europe/Malta'),
  _Timezone(zoneId: 'Europe/Mariehamn'),
  _Timezone(zoneId: 'Europe/Minsk'),
  _Timezone(zoneId: 'Europe/Monaco'),
  _Timezone(zoneId: 'Europe/Moscow'),
  _Timezone(zoneId: 'Europe/Nicosia'),
  _Timezone(zoneId: 'Europe/Oslo'),
  _Timezone(zoneId: 'Europe/Podgorica'),
  _Timezone(zoneId: 'Europe/Prague'),
  _Timezone(zoneId: 'Europe/Riga'),
  _Timezone(zoneId: 'Europe/Rome'),
  _Timezone(zoneId: 'Europe/Samara'),
  _Timezone(zoneId: 'Europe/San_Marino'),
  _Timezone(zoneId: 'Europe/Sarajevo'),
  _Timezone(zoneId: 'Europe/Simferopol'),
  _Timezone(zoneId: 'Europe/Skopje'),
  _Timezone(zoneId: 'Europe/Sofia'),
  _Timezone(zoneId: 'Europe/Stockholm'),
  _Timezone(zoneId: 'Europe/Tallinn'),
  _Timezone(zoneId: 'Europe/Tirane'),
  _Timezone(zoneId: 'Europe/Tiraspol'),
  _Timezone(zoneId: 'Europe/Uzhgorod'),
  _Timezone(zoneId: 'Europe/Vaduz'),
  _Timezone(zoneId: 'Europe/Vatican'),
  _Timezone(zoneId: 'Europe/Vienna'),
  _Timezone(zoneId: 'Europe/Vilnius'),
  _Timezone(zoneId: 'Europe/Volgograd'),
  _Timezone(zoneId: 'Europe/Warsaw'),
  _Timezone(zoneId: 'Europe/Zagreb'),
  _Timezone(zoneId: 'Europe/Zaporozhye'),
  _Timezone(zoneId: 'Europe/Zurich'),
  _Timezone(zoneId: 'Factory'),
  _Timezone(zoneId: 'GB'),
  _Timezone(zoneId: 'GB-Eire'),
  _Timezone(zoneId: 'GMT'),
  _Timezone(zoneId: 'GMT+0'),
  _Timezone(zoneId: 'GMT-0'),
  _Timezone(zoneId: 'GMT0'),
  _Timezone(zoneId: 'Greenwich'),
  _Timezone(zoneId: 'HST'),
  _Timezone(zoneId: 'Hongkong'),
  _Timezone(zoneId: 'Iceland'),
  _Timezone(zoneId: 'Indian/Antananarivo'),
  _Timezone(zoneId: 'Indian/Chagos'),
  _Timezone(zoneId: 'Indian/Christmas'),
  _Timezone(zoneId: 'Indian/Cocos'),
  _Timezone(zoneId: 'Indian/Comoro'),
  _Timezone(zoneId: 'Indian/Kerguelen'),
  _Timezone(zoneId: 'Indian/Mahe'),
  _Timezone(zoneId: 'Indian/Maldives'),
  _Timezone(zoneId: 'Indian/Mauritius'),
  _Timezone(zoneId: 'Indian/Mayotte'),
  _Timezone(zoneId: 'Indian/Reunion'),
  _Timezone(zoneId: 'Iran'),
  _Timezone(zoneId: 'Israel'),
  _Timezone(zoneId: 'Jamaica'),
  _Timezone(zoneId: 'Japan'),
  _Timezone(zoneId: 'Kwajalein'),
  _Timezone(zoneId: 'Libya'),
  _Timezone(zoneId: 'MET'),
  _Timezone(zoneId: 'MST'),
  _Timezone(zoneId: 'MST7MDT'),
  _Timezone(zoneId: 'Mexico/BajaNorte'),
  _Timezone(zoneId: 'Mexico/BajaSur'),
  _Timezone(zoneId: 'Mexico/General'),
  _Timezone(zoneId: 'Mideast/Riyadh87'),
  _Timezone(zoneId: 'Mideast/Riyadh88'),
  _Timezone(zoneId: 'Mideast/Riyadh89'),
  _Timezone(zoneId: 'NZ'),
  _Timezone(zoneId: 'NZ-CHAT'),
  _Timezone(zoneId: 'Navajo'),
  _Timezone(zoneId: 'PRC'),
  _Timezone(zoneId: 'PST8PDT'),
  _Timezone(zoneId: 'Pacific/Apia'),
  _Timezone(zoneId: 'Pacific/Auckland'),
  _Timezone(zoneId: 'Pacific/Chatham'),
  _Timezone(zoneId: 'Pacific/Easter'),
  _Timezone(zoneId: 'Pacific/Efate'),
  _Timezone(zoneId: 'Pacific/Enderbury'),
  _Timezone(zoneId: 'Pacific/Fakaofo'),
  _Timezone(zoneId: 'Pacific/Fiji'),
  _Timezone(zoneId: 'Pacific/Funafuti'),
  _Timezone(zoneId: 'Pacific/Galapagos'),
  _Timezone(zoneId: 'Pacific/Gambier'),
  _Timezone(zoneId: 'Pacific/Guadalcanal'),
  _Timezone(zoneId: 'Pacific/Guam'),
  _Timezone(zoneId: 'Pacific/Honolulu'),
  _Timezone(zoneId: 'Pacific/Johnston'),
  _Timezone(zoneId: 'Pacific/Kiritimati'),
  _Timezone(zoneId: 'Pacific/Kosrae'),
  _Timezone(zoneId: 'Pacific/Kwajalein'),
  _Timezone(zoneId: 'Pacific/Majuro'),
  _Timezone(zoneId: 'Pacific/Marquesas'),
  _Timezone(zoneId: 'Pacific/Midway'),
  _Timezone(zoneId: 'Pacific/Nauru'),
  _Timezone(zoneId: 'Pacific/Niue'),
  _Timezone(zoneId: 'Pacific/Norfolk'),
  _Timezone(zoneId: 'Pacific/Noumea'),
  _Timezone(zoneId: 'Pacific/Pago_Pago'),
  _Timezone(zoneId: 'Pacific/Palau'),
  _Timezone(zoneId: 'Pacific/Pitcairn'),
  _Timezone(zoneId: 'Pacific/Ponape'),
  _Timezone(zoneId: 'Pacific/Port_Moresby'),
  _Timezone(zoneId: 'Pacific/Rarotonga'),
  _Timezone(zoneId: 'Pacific/Saipan'),
  _Timezone(zoneId: 'Pacific/Samoa'),
  _Timezone(zoneId: 'Pacific/Tahiti'),
  _Timezone(zoneId: 'Pacific/Tarawa'),
  _Timezone(zoneId: 'Pacific/Tongatapu'),
  _Timezone(zoneId: 'Pacific/Truk'),
  _Timezone(zoneId: 'Pacific/Wake'),
  _Timezone(zoneId: 'Pacific/Wallis'),
  _Timezone(zoneId: 'Pacific/Yap'),
  _Timezone(zoneId: 'Poland'),
  _Timezone(zoneId: 'Portugal'),
  _Timezone(zoneId: 'ROC'),
  _Timezone(zoneId: 'ROK'),
  _Timezone(zoneId: 'Singapore'),
  _Timezone(zoneId: 'Turkey'),
  _Timezone(zoneId: 'UCT'),
  _Timezone(zoneId: 'US/Alaska'),
  _Timezone(zoneId: 'US/Aleutian'),
  _Timezone(zoneId: 'US/Arizona'),
  _Timezone(zoneId: 'US/Central'),
  _Timezone(zoneId: 'US/East-Indiana'),
  _Timezone(zoneId: 'US/Hawaii'),
  _Timezone(zoneId: 'US/Indiana-Starke'),
  _Timezone(zoneId: 'US/Michigan'),
  _Timezone(zoneId: 'US/Mountain'),
  _Timezone(zoneId: 'US/Pacific'),
  _Timezone(zoneId: 'US/Pacific-New'),
  _Timezone(zoneId: 'US/Samoa'),
  _Timezone(zoneId: 'UTC'),
  _Timezone(zoneId: 'Universal'),
  _Timezone(zoneId: 'W-SU'),
  _Timezone(zoneId: 'WET'),
  _Timezone(zoneId: 'Zulu'),
];
