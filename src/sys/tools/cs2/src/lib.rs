use std::{fs, iter::Iterator, path::PathBuf};

/// Convenience macro for reading the contents of a directory when
/// the path is expected to exist and all its contents are expected
/// to be readable.
macro_rules! read_dir {
    ($path:expr) => {
        fs::read_dir($path)
            .expect("read_dir: could not open directory")
            .map(|x| x.expect("read_dir: entry is unreadable"))
    };
}

static SPACER: &str = "  ";

pub struct Component {
    name: String,
    hub_v2_path: PathBuf,
    children: Vec<Component>,
}

impl Component {
    pub fn new_root_component(hub_v2_path: PathBuf) -> Self {
        let mut component = Self { name: "<root>".to_string(), hub_v2_path, children: vec![] };
        component.explore();
        component
    }

    fn new(name: String, hub_v2_path: PathBuf) -> Self {
        let mut component = Self { name, hub_v2_path, children: vec![] };
        component.explore();
        component
    }

    fn explore(&mut self) {
        // TODO(xbhatnag): This is recursive, not iterative.
        let children_path = self.hub_v2_path.join("children");
        let dir: Vec<fs::DirEntry> = read_dir!(children_path).collect();
        for entry in dir {
            let path = entry.path();
            let filename = path
                .file_name()
                .expect("explore_children: invalid path")
                .to_os_string()
                .into_string()
                .expect("explore_children: unexpected characters");
            let child = Self::new(filename, path);
            self.children.push(child);
        }
    }

    pub fn generate_output(&self) -> String {
        let mut lines = vec![];
        self.append_lines(1, &mut lines);
        lines.join("\n")
    }

    fn append_lines(&self, level: usize, lines: &mut Vec<String>) {
        let space = SPACER.repeat(level - 1);
        let line = format!("{}{}", space, self.name);
        lines.push(line);
        for child in &self.children {
            child.append_lines(level + 1, lines);
        }
    }
}
