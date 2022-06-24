// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    errors::{ffx_bail, ffx_error},
    ffx_component_doctor_args::DoctorCommand,
    ffx_core::ffx_plugin,
    ffx_writer::Writer,
    fidl::Status,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_developer_remotecontrol as rc,
    fidl_fuchsia_sys2 as fsys,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
    prettytable::{cell, format::consts::FORMAT_CLEAN, row, Table},
    std::collections::BTreeMap,
    std::io::Write,
    termion::{color, style},
};

const USE_TITLE: &'static str = "Used Capability";
const EXPOSE_TITLE: &'static str = "Exposed Capability";
const UNKNOWN_TITLE: &'static str = "Unknown Capability";
const DEFAULT_SUMMARY_TEXT: &'static str = "N/A";
const CAPABILITY_COLUMN_WIDTH: usize = 35;
const SUMMARY_COLUMN_WIDTH: usize = 85;

pub async fn start_proxies(
    rcs: rc::RemoteControlProxy,
) -> Result<(fsys::RealmQueryProxy, fsys::RouteValidatorProxy)> {
    let (realm_query, query_server) = fidl::endpoints::create_proxy::<fsys::RealmQueryMarker>()
        .context("creating RealmQuery proxy")?;
    rcs.root_realm_query(query_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening root RealmQuery")?;

    let (route_validator, validator_server) =
        fidl::endpoints::create_proxy::<fsys::RouteValidatorMarker>()
            .context("creating RouteValidator proxy")?;
    rcs.root_route_validator(validator_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening root RouteValidator")?;
    Ok((realm_query, route_validator))
}

// Create a new table with the given title.
fn new_table(title: &str) -> Table {
    let mut table = Table::new();
    table.set_format(*FORMAT_CLEAN);
    table.set_titles(row!("", b->title.to_string(), b->"Error"));
    table
}

// Add the given route report into the appropriate table in the tables map.
async fn report_to_table(
    report: &fsys::RouteReport,
    tables: &mut BTreeMap<fsys::DeclType, Table>,
) -> Result<()> {
    let report_type = report.decl_type.ok_or(ffx_error!("empty report"))?;

    let table = tables.entry(report_type).or_insert_with_key(|key| match key {
        fsys::DeclType::Use => new_table(USE_TITLE),
        fsys::DeclType::Expose => new_table(EXPOSE_TITLE),
        fsys::DeclTypeUnknown!() => new_table(UNKNOWN_TITLE),
    });

    let capability =
        report.capability.as_ref().ok_or(ffx_error!("could not read report capability"))?;
    let capability = textwrap::fill(&capability, CAPABILITY_COLUMN_WIDTH);
    let summary = textwrap::fill(DEFAULT_SUMMARY_TEXT, SUMMARY_COLUMN_WIDTH);
    let check = format!("[{}{}{}]", color::Fg(color::Green), "✓", style::Reset);
    let mut row = row!(check, capability, summary);
    if let Some(error) = &report.error {
        let summary = error.summary.as_ref().ok_or(ffx_error!("could not read report summary"))?;
        let summary = textwrap::fill(summary, SUMMARY_COLUMN_WIDTH);
        let x = format!("[{}{}{}]", color::Fg(color::Red), "✗", style::Reset);
        row = row!(x, capability, FR->summary);
    };
    table.add_row(row);
    Ok(())
}

// Given the reports, construct the output tables and print them.
async fn create_tables(reports: Vec<fsys::RouteReport>) -> Result<BTreeMap<fsys::DeclType, Table>> {
    // Collect all of the reports into tables, sorted by type.
    let mut tables = BTreeMap::new();
    for report in reports {
        report_to_table(&report, &mut tables).await?;
    }
    Ok(tables)
}

/// Perform a series of diagnostic checks on a component at runtime.
#[ffx_plugin("component.experimental")]
pub async fn doctor(
    rcs: rc::RemoteControlProxy,
    mut writer: Writer,
    cmd: DoctorCommand,
) -> Result<()> {
    // Check the moniker.
    let moniker = AbsoluteMoniker::parse_str(&cmd.moniker)
        .map_err(|_| ffx_error!("Invalid moniker: {}", &cmd.moniker))?
        .to_string();
    let rel_moniker = format!(".{}", &moniker);

    // Query the Component Manager for information about this instance.
    write!(writer, "Querying component manager for {}\n", &moniker)?;
    let (realm_query, route_validator) = start_proxies(rcs).await
        .map_err(|e| ffx_error!("Error reaching the target: {}\nIs your device/emulator up and shown in `ffx target list`?", e))?;

    // Obtain the basic information about the component.
    let (info, state) = match realm_query.get_instance_info(&rel_moniker).await? {
        Ok((info, state)) => (info, state),
        Err(fcomponent::Error::InstanceNotFound) => {
            ffx_bail!("Component manager could not find an instance with the moniker: {}\n\
                       Use `ffx component list` or `ffx component show` to find the correct moniker of your instance.",
                &moniker
            );
        }
        Err(e) => {
            ffx_bail!(
                "Component manager returned an unexpected error: {:?}\n\
                 Please report this to the Component Framework team.",
                e
            );
        }
    };

    // TODO(gboone@): Support more flexible output, including:
    // 1) writing the table without wrapping so it will work with Unix pipes,
    // 2) dropping the colors so that their escape sequences don't appear in non-tty output such as
    // pipes,
    // 3) emitting structured output for machine readability [use `writer.is_machine()`].

    // Print the basic information.
    write!(writer, "URL: {}", info.url)?;
    let instance_id = if let Some(i) = info.instance_id { i } else { "None".to_string() };
    write!(writer, "Instance ID: {}\n", instance_id)?;

    if state.is_none() {
        ffx_bail!(
            "Instance is not resolved.\n\
             Use `ffx component resolve {}` to resolve this component.",
            moniker
        );
    }

    // Pull reports.
    let reports = match route_validator.validate(&rel_moniker).await? {
        Ok(reports) => reports,
        Err(e) => {
            ffx_bail!(
                "Component manager returned an unexpected error during validation: {:?}\n\
                 The state of the component instance may have changed.\n\
                 Please report this to the Component Framework team.",
                e
            );
        }
    };

    let tables = create_tables(reports).await.context("couldn't create the result tables.")?;

    // Write all of the tables to stdout.
    for (_, table) in tables.into_iter() {
        if table.len() > 0 {
            // TODO(gboone@): The table should be printed with the writer using:
            //     table.print(&mut writer)?;
            // However, text that wraps in the second column is printed in the color of the third
            // column. Investigate and fix this defect.
            table.printstd();
            println!("");
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        prettytable::{Cell, Row},
    };

    #[test]
    fn test_new_table() {
        let mut t = new_table("foo");
        assert_eq!(t.to_string().replace("\r\n", "\n"), "\n");
        // PrettyTable needs a row, not just titles, to retrieve titles.
        t.add_row(Row::new(vec![Cell::new("t1"), Cell::new("t2"), Cell::new("t3")]));
        assert!(t.to_string().contains("foo"));
        assert!(t.to_string().contains("Error"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_report_to_table_for_empty_report() {
        let mut tables = BTreeMap::new();
        let report = fsys::RouteReport::EMPTY;
        let result = report_to_table(&report, &mut tables).await;
        assert_matches!(result.map_err(|e| format!("{:?}", e)), Err(e) if e.contains("empty report"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_report_to_table_for_missing_decl_type() {
        let mut tables = BTreeMap::new();
        let report =
            fsys::RouteReport { capability: Some("foo".to_string()), ..fsys::RouteReport::EMPTY };
        let result = report_to_table(&report, &mut tables).await;
        assert_matches!(result.map_err(|e| format!("{:?}", e)), Err(e) if e.contains("empty report"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_report_to_table_for_unknown_report() {
        let mut tables = BTreeMap::new();
        let report = fsys::RouteReport {
            capability: Some("foo".to_string()),
            decl_type: Some(fsys::DeclType::unknown()),
            ..fsys::RouteReport::EMPTY
        };
        assert_matches!(report_to_table(&report, &mut tables).await, Ok(()));
        let s = tables[&fsys::DeclType::unknown()].to_string();
        assert!(s.contains("Unknown Capability"));
        assert!(s.to_string().contains("Error"));
        // Checkmark "[✓]" includes color codes.
        assert!(s.to_string().contains("[\u{1b}[38;5;2m✓\u{1b}[m]"));
        assert!(s.to_string().contains("foo"));
        assert!(s.to_string().contains("N/A"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_report_to_table_for_valid_report() {
        // Valid Use report.
        let mut tables = BTreeMap::new();
        let report = fsys::RouteReport {
            capability: Some("foo".to_string()),
            decl_type: Some(fsys::DeclType::Use),
            ..fsys::RouteReport::EMPTY
        };
        assert_matches!(report_to_table(&report, &mut tables).await, Ok(()));
        assert_eq!(tables.len(), 1);
        assert_eq!(tables[&fsys::DeclType::Use].len(), 1);
        assert_eq!(
            tables[&fsys::DeclType::Use].get_row(0).unwrap()[0].get_content(),
            "[\u{1b}[38;5;2m✓\u{1b}[m]"
        );
        assert_eq!(tables[&fsys::DeclType::Use].get_row(0).unwrap()[1].get_content(), "foo");
        assert_eq!(tables[&fsys::DeclType::Use].get_row(0).unwrap()[2].get_content(), "N/A");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_report_to_table_for_valid_expose_report() {
        let mut tables = BTreeMap::new();
        let report = fsys::RouteReport {
            capability: Some("foo".to_string()),
            decl_type: Some(fsys::DeclType::Expose),
            ..fsys::RouteReport::EMPTY
        };
        assert_matches!(report_to_table(&report, &mut tables).await, Ok(()));
        assert_eq!(tables.len(), 1);
        assert_eq!(tables[&fsys::DeclType::Expose].len(), 1);
        assert_eq!(
            tables[&fsys::DeclType::Expose].get_row(0).unwrap()[0].get_content(),
            "[\u{1b}[38;5;2m✓\u{1b}[m]"
        );
        assert_eq!(tables[&fsys::DeclType::Expose].get_row(0).unwrap()[1].get_content(), "foo");
        assert_eq!(tables[&fsys::DeclType::Expose].get_row(0).unwrap()[2].get_content(), "N/A");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_write_tables_no_reports() {
        // No reports.
        let reports: Vec<fsys::RouteReport> = vec![];
        let tables = create_tables(reports).await.unwrap();
        assert_eq!(tables.len(), 0);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_write_tables_valid_reports() {
        let reports: Vec<fsys::RouteReport> = vec![
            fsys::RouteReport {
                capability: Some("foo".to_string()),
                decl_type: Some(fsys::DeclType::Use),
                ..fsys::RouteReport::EMPTY
            },
            fsys::RouteReport {
                capability: Some("foo".to_string()),
                decl_type: Some(fsys::DeclType::Expose),
                ..fsys::RouteReport::EMPTY
            },
        ];

        let tables = create_tables(reports).await.unwrap();
        assert_eq!(tables.len(), 2);
        assert_eq!(tables[&fsys::DeclType::Use].len(), 1);
        assert_eq!(
            tables[&fsys::DeclType::Use].get_row(0).unwrap()[0].get_content(),
            "[\u{1b}[38;5;2m✓\u{1b}[m]"
        );
        assert_eq!(tables[&fsys::DeclType::Use].get_row(0).unwrap()[1].get_content(), "foo");
        assert_eq!(tables[&fsys::DeclType::Use].get_row(0).unwrap()[2].get_content(), "N/A");
        assert_eq!(tables[&fsys::DeclType::Expose].len(), 1);
        assert_eq!(
            tables[&fsys::DeclType::Expose].get_row(0).unwrap()[0].get_content(),
            "[\u{1b}[38;5;2m✓\u{1b}[m]"
        );
        assert_eq!(tables[&fsys::DeclType::Expose].get_row(0).unwrap()[1].get_content(), "foo");
        assert_eq!(tables[&fsys::DeclType::Expose].get_row(0).unwrap()[2].get_content(), "N/A");
    }
}
