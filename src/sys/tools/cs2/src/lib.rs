use std::{fs, iter::Iterator, path::PathBuf};

/// Convenience macro for reading the filenames of a directory's contents when
/// the path is expected to exist and all its contents are expected
/// to be readable.
fn get_file_names(dir: fs::ReadDir) -> impl Iterator<Item = String> {
    dir.map(|entry| {
        entry
            .expect("get_file_names: entry is unreadable")
            .path()
            .file_name()
            .expect("get_file_names: invalid path")
            .to_os_string()
            .into_string()
            .expect("get_file_names: unexpected characters")
    })
}

static SPACER: &str = "  ";

pub struct Component {
    name: String,
    url: String,
    id: String,
    component_type: String,
    children: Vec<Component>,
    in_services: Vec<String>,
    out_services: Vec<String>,
    exposed_services: Vec<String>,
    used_services: Vec<String>,
}

impl Component {
    pub fn new_root_component(hub_path: PathBuf) -> Self {
        Self::explore("<root>".to_string(), hub_path)
    }

    fn explore(name: String, hub_path: PathBuf) -> Self {
        let url = fs::read_to_string(hub_path.join("url")).expect("Could not read url from hub");
        let id = fs::read_to_string(hub_path.join("id")).expect("Could not read id from hub");
        let component_type = fs::read_to_string(hub_path.join("component_type"))
            .expect("Could not read component_type from hub");

        let exec_path = hub_path.join("exec");
        let in_services = get_services(exec_path.join("in"));
        let out_services = get_services(exec_path.join("out"));
        let exposed_services = get_services(exec_path.join("exposed"));
        let used_services = get_services(exec_path.join("used"));

        // Recurse on the children
        let child_path = hub_path.join("children");
        let child_dir = fs::read_dir(child_path).expect("could not open children directory");
        let child_names = get_file_names(child_dir);
        let mut children: Vec<Self> = vec![];
        for child_name in child_names {
            let path = hub_path.join("children").join(child_name.clone());
            let child = Self::explore(child_name, path);
            children.push(child);
        }

        Self {
            name,
            url,
            id,
            component_type,
            children,
            in_services,
            out_services,
            exposed_services,
            used_services,
        }
    }

    pub fn generate_output(&self) -> Vec<String> {
        let mut output: Vec<String> = vec![];
        self.generate_tree(&mut output);
        output.push("".to_string());
        self.generate_details(&mut output);
        output
    }

    pub fn generate_tree(&self, lines: &mut Vec<String>) {
        self.generate_tree_recursive(1, lines);
    }

    fn generate_tree_recursive(&self, level: usize, lines: &mut Vec<String>) {
        let space = SPACER.repeat(level - 1);
        let line = format!("{}{}", space, self.name);
        lines.push(line);
        for child in &self.children {
            child.generate_tree_recursive(level + 1, lines);
        }
    }

    fn generate_details(&self, lines: &mut Vec<String>) {
        self.generate_details_recursive("", lines);
    }

    fn generate_details_recursive(&self, prefix: &str, lines: &mut Vec<String>) {
        let moniker = format!("{}{}:{}", prefix, self.name, self.id);

        lines.push(moniker.clone());
        lines.push(format!("- URL: {}", self.url));
        lines.push(format!("- Component Type: {}", self.component_type));
        generate_services("Exposed Services", &self.exposed_services, lines);
        generate_services("Incoming Services", &self.in_services, lines);
        generate_services("Outgoing Services", &self.out_services, lines);
        generate_services("Used Services", &self.used_services, lines);

        // Recurse on children
        let prefix = format!("{}/", moniker);
        for child in &self.children {
            lines.push("".to_string());
            child.generate_details_recursive(&prefix, lines);
        }
    }
}

fn get_services(path: PathBuf) -> Vec<String> {
    if let Ok(dir) = fs::read_dir(path.join("svc")) {
        get_file_names(dir).collect()
    } else {
        vec![]
    }
}

fn generate_services(services_type: &str, services: &Vec<String>, lines: &mut Vec<String>) {
    lines.push(format!("- {} ({})", services_type, services.len()));
    for service in services {
        lines.push(format!("{}- {}", SPACER, service));
    }
}
