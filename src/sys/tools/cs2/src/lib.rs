use std::{fs, iter::Iterator, path::PathBuf};

/// Convenience macro for reading the filenames of a directory's contents when
/// the path is expected to exist and all its contents are expected
/// to be readable.
macro_rules! read_dir_names {
    ($path:expr) => {
        fs::read_dir($path).expect("read_dir_names: could not open directory").map(|entry| {
            entry
                .expect("read_dir_names: entry is unreadable")
                .path()
                .file_name()
                .expect("read_dir_names: invalid path")
                .to_os_string()
                .into_string()
                .expect("read_dir_names: unexpected characters")
        })
    };
}

static SPACER: &str = "  ";

pub struct Component {
    name: String,
    url: String,
    id: String,
    component_type: String,
    children: Vec<Component>,
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

        // Recurse on the children
        let children_dir: Vec<String> = read_dir_names!(hub_path.join("children")).collect();
        let mut children: Vec<Self> = vec![];
        for child_name in children_dir {
            let path = hub_path.join("children").join(child_name.clone());
            let child = Self::explore(child_name, path);
            children.push(child);
        }

        Self { name, url, id, component_type, children }
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

        // Recurse on children
        let prefix = format!("{}/", moniker);
        for child in &self.children {
            lines.push("".to_string());
            child.generate_details_recursive(&prefix, lines);
        }
    }
}
