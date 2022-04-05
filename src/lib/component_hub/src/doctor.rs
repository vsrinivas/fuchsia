// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    anyhow::Result,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
    std::collections::BTreeSet,
    std::iter::FromIterator,
};

pub struct DoctorComponent {
    outgoing: Vec<String>,
    exposed: Vec<String>,

    pub failed: bool,
}

impl DoctorComponent {
    pub async fn from_moniker(moniker: AbsoluteMoniker, hub_dir: &Directory) -> Result<Self> {
        let parts = moniker.path();

        let mut dir = hub_dir.clone()?;

        for part in parts {
            dir = dir.open_dir_readable("children")?;
            dir = dir.open_dir_readable(&part.name)?;
        }

        let exposed = if dir.exists("resolved").await? {
            let resolved_dir = dir.open_dir_readable("resolved")?;
            let expose_dir = resolved_dir.open_dir_readable("expose")?;
            get_capabilities(expose_dir).await?
        } else {
            vec![]
        };

        let outgoing = if dir.exists("exec").await? {
            let exec_dir = dir.open_dir_readable("exec")?;
            let out_dir = exec_dir.open_dir_readable("out")?;
            get_capabilities(out_dir).await?
        } else {
            vec![]
        };

        Ok(DoctorComponent { exposed, outgoing, failed: false })
    }

    pub fn check_exposed_outgoing_capabilities(
        &mut self,
    ) -> ExposedOutgoingCapabilitiesCheckResult {
        let (missing_exposed, missing_outgoing) = compare_lists(&self.exposed, &self.outgoing);

        let success = missing_exposed.is_empty() && missing_outgoing.is_empty();

        self.failed |= !success;

        ExposedOutgoingCapabilitiesCheckResult { success, missing_exposed, missing_outgoing }
    }
}

// Get all entries in a capabilities directory. If there is a "svc" directory, traverse it and
// collect all protocol names as well.
async fn get_capabilities(capability_dir: Directory) -> Result<Vec<String>> {
    let mut entries = capability_dir.entries().await?;

    for (index, name) in entries.iter().enumerate() {
        if name == "svc" {
            entries.remove(index);
            let svc_dir = capability_dir.open_dir_readable("svc")?;
            let mut svc_entries = svc_dir.entries().await?;
            entries.append(&mut svc_entries);
            break;
        }
    }

    entries.sort_unstable();
    Ok(entries)
}

fn compare_lists<T>(a: &Vec<T>, b: &Vec<T>) -> (Vec<T>, Vec<T>)
where
    T: Ord + std::clone::Clone,
{
    let a_hashset = BTreeSet::from_iter(a.iter().cloned());
    let b_hashset = BTreeSet::from_iter(b.iter().cloned());

    let missing_a = b_hashset.difference(&a_hashset).cloned().collect();
    let missing_b = a_hashset.difference(&b_hashset).cloned().collect();

    (missing_a, missing_b)
}

pub struct ExposedOutgoingCapabilitiesCheckResult {
    pub success: bool,
    pub missing_exposed: Vec<String>,
    pub missing_outgoing: Vec<String>,
}

impl std::fmt::Display for ExposedOutgoingCapabilitiesCheckResult {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if self.success {
            writeln!(f, "\nSuccess! All the capabilities exposed in the component's manifest are available at runtime. The lists of `exposed` and `outgoing` capabilities match.")?;
        } else {
            if !self.missing_exposed.is_empty() {
                writeln!(f, "\nFound `outgoing` capabilities that are available at runtime, but not exposed in the manifest. Consider adding these capabilities to the `exposed` section of the component's manifest:\n")?;
                for capability in &self.missing_exposed {
                    writeln!(f, "    {}\n", capability)?;
                }
            }
            if !self.missing_outgoing.is_empty() {
                writeln!(f, "\nFound `exposed` capabilities that are not available at runtime. Consider adding these capabilities to the `out` section of the component's manifest:\n")?;
                for capability in &self.missing_outgoing {
                    writeln!(f, "    {}\n", capability)?;
                }
            }
        }

        Ok(())
    }
}
////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        anyhow::Result,
        moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
        std::fs,
        tempfile::TempDir,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn from_moniker() -> Result<()> {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- exec
        //    |- out
        //       |-debug
        //       |-minfs
        // |- resolved
        //    |- use
        //       |- dev
        //    |- expose
        //       |- minfs
        fs::create_dir_all(root.join("resolved/use/dev")).unwrap();
        fs::create_dir_all(root.join("resolved/expose/minfs")).unwrap();
        fs::create_dir_all(root.join("exec/out/debug")).unwrap();
        fs::create_dir_all(root.join("exec/out/minfs")).unwrap();

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let component = DoctorComponent::from_moniker(AbsoluteMoniker::root(), &hub_dir).await?;

        assert_eq!(component.failed, false);

        assert_eq!(component.outgoing, vec!["debug", "minfs"]);
        assert_eq!(component.exposed, vec!["minfs"]);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_compare_lists() -> Result<()> {
        let vec1 = vec!["a", "b", "c"];
        let vec2 = vec!["a", "b", "d"];

        assert_eq!(compare_lists(&vec1, &vec1), (vec![], vec![]));
        assert_eq!(compare_lists(&vec1, &vec2), (vec!["d"], vec!["c"]));
        assert_eq!(compare_lists(&vec2, &vec1), (vec!["c"], vec!["d"]));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn doctor_exposed_outgoing_capabilities_success() -> Result<()> {
        let mut doctor_comp = DoctorComponent {
            exposed: vec!["fuchsia.foo.bar1".to_string(), "fuchsia.foo.bar2".to_string()],
            outgoing: vec!["fuchsia.foo.bar1".to_string(), "fuchsia.foo.bar2".to_string()],
            failed: false,
        };

        let res = doctor_comp.check_exposed_outgoing_capabilities();

        assert!(!doctor_comp.failed);
        assert!(res.success);
        assert!(res.missing_exposed.is_empty());
        assert!(res.missing_outgoing.is_empty());

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn doctor_exposed_outgoing_capabilities_fail() -> Result<()> {
        let mut doctor_comp = DoctorComponent {
            exposed: vec![
                "fuchsia.foo.bar1".to_string(),
                "fuchsia.foo.bar2".to_string(),
                "fuchsia.foo.bar3".to_string(),
            ],
            outgoing: vec![
                "fuchsia.foo.bar0".to_string(),
                "fuchsia.foo.bar1".to_string(),
                "fuchsia.foo.bar2".to_string(),
                "fuchsia.foo.bar4".to_string(),
            ],
            failed: false,
        };

        let res = doctor_comp.check_exposed_outgoing_capabilities();

        assert!(doctor_comp.failed);
        assert!(!res.success);
        assert_eq!(
            res.missing_exposed,
            vec!["fuchsia.foo.bar0".to_string(), "fuchsia.foo.bar4".to_string()]
        );
        assert_eq!(res.missing_outgoing, vec!["fuchsia.foo.bar3".to_string()]);

        Ok(())
    }
}
