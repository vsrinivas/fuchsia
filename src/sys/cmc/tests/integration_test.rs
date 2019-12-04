use cm_fidl_translator;
use failure::Error;
use fidl_fuchsia_data as fd;
use fidl_fuchsia_io2 as fio2;
use fidl_fuchsia_sys2::{
    ChildDecl, ChildRef, CollectionDecl, CollectionRef, ComponentDecl, Durability, ExposeDecl,
    ExposeDirectoryDecl, ExposeServiceProtocolDecl, ExposeServiceDecl, FrameworkRef, OfferDecl,
    OfferServiceProtocolDecl, OfferServiceDecl, RealmRef, Ref, SelfRef, StartupMode, UseDecl,
    UseServiceProtocolDecl, UseServiceDecl,
};
use std::fs::File;
use std::io::Read;
use std::path::PathBuf;

fn main() {
    let cm_content = read_cm("/pkg/meta/example.cm").expect("could not open example.cm");
    let golden_cm = read_cm("/pkg/data/golden.cm").expect("could not open golden.cm");
    assert_eq!(&cm_content, &golden_cm);

    let cm_decl = cm_fidl_translator::translate(&cm_content).expect("could not translate cm");
    let expected_decl = {
        let program = fd::Dictionary {
            entries: vec![fd::Entry {
                key: "binary".to_string(),
                value: Some(Box::new(fd::Value::Str("bin/example".to_string()))),
            }],
        };
        let uses = vec![
            UseDecl::Service(UseServiceDecl {
                source: Some(Ref::Realm(RealmRef {})),
                source_path: Some("/fonts/CoolFonts".to_string()),
                target_path: Some("/svc/fuchsia.fonts.Provider".to_string()),
            }),
            UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                source: Some(Ref::Realm(RealmRef {})),
                source_path: Some("/fonts/LegacyCoolFonts".to_string()),
                target_path: Some("/svc/fuchsia.fonts.LegacyProvider".to_string()),
            }),
        ];
        let exposes = vec![
            ExposeDecl::Service(ExposeServiceDecl {
                source: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                source_path: Some("/loggers/fuchsia.logger.Log".to_string()),
                target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                target: Some(Ref::Realm(RealmRef {})),
            }),
            ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                source: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                source_path: Some("/loggers/fuchsia.logger.LegacyLog".to_string()),
                target_path: Some("/svc/fuchsia.logger.LegacyLog".to_string()),
                target: Some(Ref::Realm(RealmRef {})),
            }),
            ExposeDecl::Directory(ExposeDirectoryDecl {
                source: Some(Ref::Self_(SelfRef {})),
                source_path: Some("/volumes/blobfs".to_string()),
                target_path: Some("/volumes/blobfs".to_string()),
                target: Some(Ref::Framework(FrameworkRef {})),
                rights: Some(
                    fio2::Operations::Connect
                        | fio2::Operations::ReadBytes
                        | fio2::Operations::WriteBytes
                        | fio2::Operations::GetAttributes
                        | fio2::Operations::UpdateAttributes
                        | fio2::Operations::Enumerate
                        | fio2::Operations::Traverse
                        | fio2::Operations::ModifyDirectory,
                ),
            }),
        ];
        let offers = vec![
            OfferDecl::Service(OfferServiceDecl {
                source: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                source_path: Some("/svc/fuchsia.logger.Log".to_string()),
                target: Some(Ref::Collection(CollectionRef { name: "modular".to_string() })),
                target_path: Some("/svc/fuchsia.logger.Log".to_string()),
            }),
            OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                source: Some(Ref::Child(ChildRef { name: "logger".to_string(), collection: None })),
                source_path: Some("/svc/fuchsia.logger.LegacyLog".to_string()),
                target: Some(Ref::Collection(CollectionRef { name: "modular".to_string() })),
                target_path: Some("/svc/fuchsia.logger.LegacyLog".to_string()),
            }),
        ];
        let children = vec![ChildDecl {
            name: Some("logger".to_string()),
            url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
            startup: Some(StartupMode::Lazy),
        }];
        let collections = vec![CollectionDecl {
            name: Some("modular".to_string()),
            durability: Some(Durability::Persistent),
        }];
        let facets = fd::Dictionary {
            entries: vec![
                fd::Entry {
                    key: "author".to_string(),
                    value: Some(Box::new(fd::Value::Str("Fuchsia".to_string()))),
                },
                fd::Entry { key: "year".to_string(), value: Some(Box::new(fd::Value::Inum(2018))) },
            ],
        };
        // TODO: test storage
        ComponentDecl {
            program: Some(program),
            uses: Some(uses),
            exposes: Some(exposes),
            offers: Some(offers),
            children: Some(children),
            collections: Some(collections),
            facets: Some(facets),
            storage: None,
            // TODO(fxb/4761): Test runners.
            runners: None,
        }
    };
    assert_eq!(cm_decl, expected_decl);
}

fn read_cm(file: &str) -> Result<String, Error> {
    let mut buffer = String::new();
    let path = PathBuf::from(file);
    File::open(&path)?.read_to_string(&mut buffer)?;
    Ok(buffer)
}
