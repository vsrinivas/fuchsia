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
            RepeatedTimeConversion, SkippedTimeConversion, TimeZoneId, TimeZoneInfo,
            TimeZonesProxy,
        },
        fuchsia_async as fasync,
        fuchsia_component_test::{
            builder::{Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint},
            RealmInstance,
        },
        fuchsia_zircon as zx,
    };

    static SVC_URL: &str =
        "fuchsia-pkg://fuchsia.com/time-zone-info-service-test#meta/time-zone-info-service.cmx";

    static TZ_NYC: &str = "America/New_York";
    static NANOS_PER_SECOND: i64 = 1_000_000_000;
    static SECONDS_PER_HOUR: i64 = 3600;

    async fn connect_to_service() -> Result<(RealmInstance, TimeZonesProxy)> {
        const MONIKER: &str = "tzinfo";

        let mut builder = RealmBuilder::new().await?;
        builder
            .add_component(MONIKER, ComponentSource::LegacyUrl(SVC_URL.to_string()))
            .await?
            .add_route(CapabilityRoute {
                capability: Capability::protocol(fintl::TimeZonesMarker::PROTOCOL_NAME),
                source: RouteEndpoint::component(MONIKER),
                targets: vec![RouteEndpoint::AboveRoot],
            })?;

        let realm = builder.build().create().await?;
        let svc = realm.root.connect_to_protocol_at_exposed_dir::<fintl::TimeZonesMarker>()?;
        Ok((realm, svc))
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_civil_to_absolute_time() -> Result<()> {
        let (realm, svc) = connect_to_service().await?;

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

        let actual =
            svc.civil_to_absolute_time(civil_time, options).await?.map(zx::Time::from_nanos);
        realm.destroy().await?;

        let expected = Ok(zx::Time::from_nanos(1629073062 * NANOS_PER_SECOND + 123_456_789));
        assert_eq!(actual, expected);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_absolute_to_civil_time() -> Result<()> {
        let (realm, svc) = connect_to_service().await?;

        let absolute_time = 1629073062 * NANOS_PER_SECOND + 123_456_789;
        let mut tz_id = TimeZoneId { id: TZ_NYC.to_string() };

        let actual = svc.absolute_to_civil_time(&mut tz_id, absolute_time).await?;
        realm.destroy().await?;

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
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_time_zone_info() -> Result<()> {
        let (realm, svc) = connect_to_service().await?;

        let absolute_time = 1629073062 * NANOS_PER_SECOND + 123_456_789;
        let mut tz_id = TimeZoneId { id: TZ_NYC.to_string() };

        let actual = svc.get_time_zone_info(&mut tz_id, absolute_time).await?;
        realm.destroy().await?;

        let expected = Ok(fable! {
          TimeZoneInfo {
              id: tz_id,
              total_offset_at_time: -4 * SECONDS_PER_HOUR * NANOS_PER_SECOND,
          }
        });
        assert_eq!(actual, expected);
        Ok(())
    }
}
