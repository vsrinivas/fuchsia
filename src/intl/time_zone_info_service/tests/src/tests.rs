// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod tests {
    use {
        anyhow::Result,
        fable_lib::fable,
        fidl::endpoints::DiscoverableProtocolMarker,
        fidl_fuchsia_intl::{
            self as fintl, CivilTime, CivilToAbsoluteTimeOptions, DayOfWeek, Month,
            RepeatedTimeConversion, SkippedTimeConversion, TimeZoneId,
        },
        fuchsia_async as fasync,
        fuchsia_component_test::{
            builder::{Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint},
            error::Error as RealmBuilderError,
            RealmInstance,
        },
        fuchsia_zircon as zx,
    };

    static TZ_NYC: &str = "America/New_York";
    static NANOS_PER_SECOND: i64 = 1_000_000_000;
    static TIMEZONE_URL: &str =
        "fuchsia-pkg://fuchsia.com/time-zone-info-service-test#meta/time-zone-info-service.cmx";

    async fn build_time_zone_realm() -> Result<RealmInstance, RealmBuilderError> {
        let mut builder = RealmBuilder::new().await?;
        builder
            .add_component("timezone", ComponentSource::LegacyUrl(TIMEZONE_URL.to_string()))
            .await?
            .add_route(CapabilityRoute {
                capability: Capability::protocol(fintl::TimeZonesMarker::PROTOCOL_NAME),
                source: RouteEndpoint::component("timezone"),
                targets: vec![RouteEndpoint::AboveRoot],
            })?;

        builder.build().create().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_civil_to_absolute_time() -> Result<()> {
        let civil_time = fable! {
          CivilTime {
              year: 2021,
              month: Month::August,
              day: 15,
              hour: 20,
              minute: 17,
              second: 42,
              nanos: 123_456_789,
              time_zone_id: TimeZoneId {
                  id: TZ_NYC.to_string(),
              },
          }
        };

        let options = fable! {
            CivilToAbsoluteTimeOptions {
                repeated_time_conversion: RepeatedTimeConversion::BeforeTransition,
                skipped_time_conversion: SkippedTimeConversion::NextValidTime,
            }
        };

        let time_zone_realm = build_time_zone_realm().await?;

        let timezone_svc =
            time_zone_realm.root.connect_to_protocol_at_exposed_dir::<fintl::TimeZonesMarker>()?;

        let actual = timezone_svc
            .civil_to_absolute_time(civil_time, options)
            .await?
            .map(zx::Time::from_nanos);
        let expected = Ok(zx::Time::from_nanos(1629073062 * NANOS_PER_SECOND + 123_456_789));

        assert_eq!(actual, expected);
        time_zone_realm.destroy().await.unwrap();
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_absolute_to_civil_time() -> Result<()> {
        let absolute_time = 1629073062 * NANOS_PER_SECOND + 123_456_789;
        let mut tz_id = TimeZoneId { id: TZ_NYC.to_string() };

        let time_zone_realm = build_time_zone_realm().await?;

        let timezone_svc =
            time_zone_realm.root.connect_to_protocol_at_exposed_dir::<fintl::TimeZonesMarker>()?;
        let actual = timezone_svc.absolute_to_civil_time(&mut tz_id, absolute_time).await?;

        let expected = Ok(fable! {
          CivilTime {
              year: 2021,
              month: Month::August,
              day: 15,
              hour: 20,
              minute: 17,
              second: 42,
              nanos: 123_456_789,
              weekday: DayOfWeek::Sunday,
              year_day: 226,
              time_zone_id: TimeZoneId {
                  id: TZ_NYC.to_string(),
              },
          }
        });

        assert_eq!(actual, expected);
        time_zone_realm.destroy().await.unwrap();

        Ok(())
    }
}
