
## **TABLES**

### RegulatoryDomain {#RegulatoryDomain}


*Defined in [fuchsia.intl/intl.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.intl/intl.fidl#8)*

 Typed identifier for a regulatory domain as specified in the IEEE 802.11 standard.


<table>
    <tr><th>Ordinal</th><th>Name</th><th>Type</th><th>Description</th></tr>
    <tr>
            <td>1</td>
            <td><code>country_code</code></td>
            <td>
                <code>string</code>
            </td>
            <td> ISO 3166-1 alpha-2, a two-letter code representing a domain of operation.
 (https://www.iso.org/publication/PUB500001.html)
</td>
        </tr></table>

### Profile {#Profile}


*Defined in [fuchsia.intl/intl.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.intl/intl.fidl#69)*

 A collection of ranked internationalization properties.

 There is no implied origin for this information; it might come from a user account, device
 settings, a synthesis of user settings and app-specific overrides, or anywhere else.

 Language-independent properties that are supported by Unicode BCP-47 Locale IDs (e.g.
 first-day-of-week, time zone) are denormalized into the locale IDs in `locales`.


<table>
    <tr><th>Ordinal</th><th>Name</th><th>Type</th><th>Description</th></tr>
    <tr>
            <td>1</td>
            <td><code>locales</code></td>
            <td>
                <code>vector&lt;<a class='link' href='#LocaleId'>LocaleId</a>&gt;</code>
            </td>
            <td> Ranked list of locales (in descending order).
</td>
        </tr><tr>
            <td>2</td>
            <td><code>calendars</code></td>
            <td>
                <code>vector&lt;<a class='link' href='#CalendarId'>CalendarId</a>&gt;</code>
            </td>
            <td> Ranked list of calendars (in descending order). The first entry is the primary calendar, and
 will be equal to the calendar indicated in `locales`.
 The list is intended for use by applications that can display multiple calendar systems.
</td>
        </tr><tr>
            <td>3</td>
            <td><code>time_zones</code></td>
            <td>
                <code>vector&lt;<a class='link' href='#TimeZoneId'>TimeZoneId</a>&gt;</code>
            </td>
            <td> Ranked list of time zones (in descending order). The first entry is the primary time zone,
 which should be used by default for formatting dates and times; it will be equal to the
 calendar indicated in `locales`.
 The list is intended for use by applications that can display multiple time zones, e.g.
 a world clock.
</td>
        </tr><tr>
            <td>4</td>
            <td><code>temperature_unit</code></td>
            <td>
                <code><a class='link' href='#TemperatureUnit'>TemperatureUnit</a></code>
            </td>
            <td> Selected temperature unit.
</td>
        </tr></table>
