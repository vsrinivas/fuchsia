// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod diagnosis;

use {
    crate::diagnosis::{Analysis, Diagnosis},
    anyhow::{Context, Result},
    errors::{ffx_bail, ffx_error},
    ffx_component::{
        query::get_cml_moniker_from_query,
        rcs::{connect_to_realm_explorer, connect_to_realm_query, connect_to_route_validator},
    },
    ffx_component_doctor_args::DoctorCommand,
    ffx_core::ffx_plugin,
    ffx_writer::Writer,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_sys2 as fsys,
    prettytable::{cell, format::consts::FORMAT_CLEAN, row, Row, Table},
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

// Create a new table with the given title.
fn new_table(title: &str) -> Table {
    let mut table = Table::new();
    table.set_format(*FORMAT_CLEAN);
    table.set_titles(row!("", b->title.to_string(), b->"Error"));
    table
}

// Add the given route report into the appropriate table in the tables map.
async fn report_to_diagnosis(report: fsys::RouteReport) -> Result<Diagnosis> {
    let decl_type = match report.decl_type.ok_or(ffx_error!("empty report"))? {
        fsys::DeclType::Use => "use".to_string(),
        fsys::DeclType::Expose => "expose".to_string(),
        _ => "unknown".to_string(),
    };
    let capability = report.capability.ok_or(ffx_error!("could not read report capability"))?;
    let mut summary = None;
    let mut is_error = false;
    if let Some(error) = &report.error {
        summary = Some(
            error.summary.as_ref().ok_or(ffx_error!("could not read report summary"))?.to_string(),
        );
        is_error = true;
    };
    Ok(Diagnosis { decl_type, is_error, capability, summary })
}

// The following four functions format a Diagnosis based on whether it needs to be plain or not and
// whether it contains an error or not.
fn format_not_plain_no_error(capability: String, summary: String) -> Row {
    let mark = format!("[{}{}{}]", color::Fg(color::Green), "✓", style::Reset);
    let capability = textwrap::fill(&capability, CAPABILITY_COLUMN_WIDTH);
    let summary = textwrap::fill(&summary, SUMMARY_COLUMN_WIDTH);
    row!(mark, capability, summary)
}

fn format_not_plain_error(capability: String, summary: String) -> Row {
    let mark = format!("[{}{}{}]", color::Fg(color::Red), "✗", style::Reset);
    let capability = textwrap::fill(&capability, CAPABILITY_COLUMN_WIDTH);
    let summary = textwrap::fill(&summary, SUMMARY_COLUMN_WIDTH);
    row!(mark, capability, FR->summary) // `FR->` macro makes it red.
}

fn format_plain_no_error(capability: String, summary: String) -> Row {
    row!("[✓]".to_string(), capability, summary)
}

fn format_plain_error(capability: String, summary: String) -> Row {
    row!("[✗]".to_string(), capability, summary)
}

pub fn format(capability: String, summary: String, plain_output: bool, is_error: bool) -> Row {
    match (plain_output, is_error) {
        (false, false) => format_not_plain_no_error(capability, summary),
        (false, true) => format_not_plain_error(capability, summary),
        (true, false) => format_plain_no_error(capability, summary),
        (true, true) => format_plain_error(capability, summary),
    }
}

// Define the table display of the diagnoses.
async fn add_diagnosis_to_table(
    diagnosis: Diagnosis,
    tables: &mut BTreeMap<fsys::DeclType, Table>,
    plain_output: bool,
) -> Result<()> {
    let decl_type = match diagnosis.decl_type.as_ref() {
        "use" => 1,
        "expose" => 2,
        _ => 0,
    };
    let table = tables
        .entry(fsys::DeclType::from_primitive_allow_unknown(decl_type))
        .or_insert_with_key(|key| match key {
            fsys::DeclType::Use => new_table(USE_TITLE),
            fsys::DeclType::Expose => new_table(EXPOSE_TITLE),
            fsys::DeclTypeUnknown!() => new_table(UNKNOWN_TITLE),
        });

    let capability = diagnosis.capability;
    let summary = diagnosis.summary.unwrap_or(DEFAULT_SUMMARY_TEXT.to_string());
    let row = format(capability, summary, plain_output, diagnosis.is_error);
    table.add_row(row);
    Ok(())
}

// Analyze the reports into diagnoses.
async fn diagnose(reports: Vec<fsys::RouteReport>) -> Result<Vec<Diagnosis>> {
    let mut diagnoses = vec![];
    for report in reports {
        let diagnosis = report_to_diagnosis(report).await?;
        diagnoses.push(diagnosis);
    }
    Ok(diagnoses)
}

// Given the reports, construct the output tables and print them.
async fn create_tables(
    diagnoses: Vec<Diagnosis>,
    plain_output: bool,
) -> Result<BTreeMap<fsys::DeclType, Table>> {
    let mut tables = BTreeMap::new();
    for diagnosis in diagnoses {
        add_diagnosis_to_table(diagnosis, &mut tables, plain_output).await?;
    }
    Ok(tables)
}

// Write all of the tables to stdout.
async fn print_tables(mut writer: Writer, analysis: Analysis, plain_output: bool) -> Result<()> {
    // Print the basic information.
    write!(writer, "URL: {}\n", analysis.url)?;
    let instance_id = if let Some(i) = analysis.instance_id { i } else { "None".to_string() };
    write!(writer, "Instance ID: {}\n\n", instance_id)?;

    // Print the tables.
    let tables = create_tables(analysis.diagnoses, plain_output)
        .await
        .context("couldn't create the result tables.")?;
    for (_, table) in tables.into_iter() {
        if table.len() > 0 {
            // TODO(fxbug.dev/104187): The table should be printed with the writer using:
            //     table.print(&mut writer)?;
            // However, text that wraps in the second column is printed in the color of the third
            // column.
            table.printstd();
            println!("");
        }
    }
    Ok(())
}

/// Perform a series of diagnostic checks on a component at runtime.
#[ffx_plugin()]
pub async fn doctor(
    rcs: rc::RemoteControlProxy,
    cmd: DoctorCommand,
    #[ffx(machine = Analysis)] mut writer: Writer,
) -> Result<()> {
    let realm_explorer = connect_to_realm_explorer(&rcs).await?;
    let realm_query = connect_to_realm_query(&rcs).await?;
    let route_validator = connect_to_route_validator(&rcs).await?;

    // Check the moniker.
    let moniker = get_cml_moniker_from_query(&cmd.query, &realm_explorer).await?;
    let rel_moniker = format!(".{}", &moniker);

    // Query the Component Manager for information about this instance.
    writeln!(writer, "Moniker: {}", &moniker)?;

    // Obtain the basic information about the component.
    let (info, state) = match realm_query.get_instance_info(&rel_moniker).await? {
        Ok((info, state)) => (info, state),
        Err(fsys::RealmQueryError::InstanceNotFound) => {
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

    if state.is_none() {
        ffx_bail!(
            "Instance is not resolved.\n\
             Use `ffx component resolve {}` to resolve this component.",
            moniker
        );
    }

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

    let analysis = Analysis {
        url: info.url,
        instance_id: info.instance_id,
        diagnoses: diagnose(reports).await?,
    };

    if writer.is_machine() {
        writer.machine(&analysis).context("writing machine representation of analysis")
    } else {
        print_tables(writer, analysis, cmd.plain_output).await
    }
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
    async fn test_report_to_diagnosis_for_empty_report() {
        let report = fsys::RouteReport::EMPTY;
        let result = report_to_diagnosis(report).await;
        assert_matches!(result.map_err(|e| format!("{:?}", e)), Err(e) if e.contains("empty report"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_report_to_diagnosis_for_valid_report() {
        let report = fsys::RouteReport {
            capability: Some("foo".to_string()),
            decl_type: Some(fsys::DeclType::Use),
            ..fsys::RouteReport::EMPTY
        };
        let diagnosis = report_to_diagnosis(report).await.unwrap();
        assert_eq!(diagnosis.is_error, false);
        assert_eq!(diagnosis.decl_type, "use".to_string());
        assert_eq!(diagnosis.capability, "foo".to_string());
        assert_eq!(diagnosis.summary, None);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_diagnosis_to_table_with_color_and_wrap() {
        let mut tables = BTreeMap::new();
        let diagnosis = Diagnosis {
            is_error: false,
            decl_type: "use".to_string(),
            capability: "a123456789b123456789c123456789d123456789e123456789".to_string(),
            summary: None,
        };
        assert_matches!(add_diagnosis_to_table(diagnosis, &mut tables, false).await, Ok(()));

        let s = tables[&fsys::DeclType::Use].to_string();
        assert!(s.contains("Used"));
        assert!(s.contains("Capability"));
        assert!(s.contains("Error"));
        // Checkmark "[✓]" includes color codes.
        assert!(s.contains("[\u{1b}[38;5;2m✓\u{1b}[m]"));
        assert!(s.contains("a123456789b123456789c123456789d1234 ")); // The space is where it wraps.
        assert!(s.contains("N/A"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_diagnosis_to_table_plain() {
        let mut tables = BTreeMap::new();
        let diagnosis = Diagnosis {
            is_error: false,
            decl_type: "use".to_string(),
            capability: "a123456789b123456789c123456789d123456789e123456789".to_string(),
            summary: None,
        };
        assert_matches!(add_diagnosis_to_table(diagnosis, &mut tables, true).await, Ok(()));

        let s = tables[&fsys::DeclType::Use].to_string();
        assert!(s.contains("Used"));
        assert!(s.contains("Capability"));
        assert!(s.contains("Error"));
        assert!(s.contains("[✓]"));
        assert!(s.contains("a123456789b123456789c123456789d123456789e123456789")); // No wrap.
        assert!(s.contains("N/A"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_diagnosis_to_table_for_error() {
        let mut tables = BTreeMap::new();
        let diagnosis = Diagnosis {
            is_error: true,
            decl_type: "use".to_string(),
            capability: "foo".to_string(),
            summary: Some("bar".to_string()),
        };
        assert_matches!(add_diagnosis_to_table(diagnosis, &mut tables, false).await, Ok(()));

        let s = tables[&fsys::DeclType::Use].to_string();
        // Error mark "[x]" includes color codes.
        assert!(s.contains("[\u{1b}[38;5;1m✗\u{1b}[m]"));
        assert!(s.contains("foo"));
        assert!(s.contains("bar"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_diagnosis_to_table_for_error_plain_output() {
        let mut tables = BTreeMap::new();
        let diagnosis = Diagnosis {
            is_error: true,
            decl_type: "use".to_string(),
            capability: "foo".to_string(),
            summary: Some("bar".to_string()),
        };
        assert_matches!(add_diagnosis_to_table(diagnosis, &mut tables, true).await, Ok(()));

        let s = tables[&fsys::DeclType::Use].to_string();
        assert!(s.contains("[✗]"));
        assert!(s.contains("foo"));
        assert!(s.contains("bar"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_create_tables_no_diagnoses() {
        // No diagnoses.
        let diagnoses: Vec<Diagnosis> = vec![];
        let tables = create_tables(diagnoses, false).await.unwrap();
        assert_eq!(tables.len(), 0);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_create_tables_valid_diagnoses() {
        let diagnoses: Vec<Diagnosis> = vec![
            Diagnosis {
                is_error: false,
                decl_type: "use".to_string(),
                capability: "foo".to_string(),
                summary: None,
            },
            Diagnosis {
                is_error: true,
                decl_type: "expose".to_string(),
                capability: "foo2".to_string(),
                summary: Some("bar2".to_string()),
            },
        ];

        let tables = create_tables(diagnoses, false).await.unwrap();
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
            "[\u{1b}[38;5;1m✗\u{1b}[m]"
        );
        assert_eq!(tables[&fsys::DeclType::Expose].get_row(0).unwrap()[1].get_content(), "foo2");
        assert_eq!(tables[&fsys::DeclType::Expose].get_row(0).unwrap()[2].get_content(), "bar2");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_create_tables_valid_diagnoses_plain() {
        let diagnoses: Vec<Diagnosis> = vec![
            Diagnosis {
                is_error: false,
                decl_type: "use".to_string(),
                capability: "foo".to_string(),
                summary: None,
            },
            Diagnosis {
                is_error: true,
                decl_type: "expose".to_string(),
                capability: "foo2".to_string(),
                summary: Some("bar2".to_string()),
            },
        ];

        let tables = create_tables(diagnoses, true).await.unwrap();
        assert_eq!(tables.len(), 2);
        assert_eq!(tables[&fsys::DeclType::Use].len(), 1);
        assert_eq!(tables[&fsys::DeclType::Use].get_row(0).unwrap()[0].get_content(), "[✓]");
        assert_eq!(tables[&fsys::DeclType::Use].get_row(0).unwrap()[1].get_content(), "foo");
        assert_eq!(tables[&fsys::DeclType::Use].get_row(0).unwrap()[2].get_content(), "N/A");
        assert_eq!(tables[&fsys::DeclType::Expose].len(), 1);
        assert_eq!(tables[&fsys::DeclType::Expose].get_row(0).unwrap()[0].get_content(), "[✗]");
        assert_eq!(tables[&fsys::DeclType::Expose].get_row(0).unwrap()[1].get_content(), "foo2");
        assert_eq!(tables[&fsys::DeclType::Expose].get_row(0).unwrap()[2].get_content(), "bar2");
    }
}
