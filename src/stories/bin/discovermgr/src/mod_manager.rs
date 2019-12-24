// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        models::{
            Action, AddModInfo, OutputConsumer, Parameter, RestoreStoryInfo, SuggestedAction,
            Suggestion,
        },
        story_context_store::{ContextReader, ContextWriter, Contributor},
        story_manager::StoryManager,
    },
    anyhow::Error,
    fidl_fuchsia_modular::{
        ExecuteStatus, FocusMod, PuppetMasterProxy, SetFocusState, StoryCommand,
        StoryPuppetMasterMarker,
    },
    futures::future::join_all,
    parking_lot::Mutex,
    std::{
        collections::{HashMap, HashSet},
        sync::Arc,
    },
};

type EntityReference = String;

/// Starts mods and tracks the entity references in order to reissue intents
/// when entities change.
pub struct ModManager<T> {
    // Maps an entity reference to actions launched with it.
    pub(super) actions: HashMap<EntityReference, HashSet<AddModInfo>>,
    story_context_store: Arc<Mutex<T>>,
    puppet_master: PuppetMasterProxy,
    pub story_manager: Arc<Mutex<StoryManager>>,
    available_actions: Arc<Vec<Action>>,
}

impl<T: ContextReader + ContextWriter> ModManager<T> {
    pub fn new(
        story_context_store: Arc<Mutex<T>>,
        puppet_master: PuppetMasterProxy,
        story_manager: Arc<Mutex<StoryManager>>,
        available_actions: Arc<Vec<Action>>,
    ) -> Self {
        ModManager {
            actions: HashMap::new(),
            story_context_store,
            puppet_master,
            story_manager,
            available_actions,
        }
    }

    pub async fn execute_suggestion(&mut self, suggestion: Suggestion) -> Result<(), Error> {
        match suggestion.action() {
            SuggestedAction::AddMod(action) => {
                self.issue_action(&action, /* focus= */ true).await
            }
            SuggestedAction::RestoreStory(restore_story_info) => {
                self.handle_restory_story(restore_story_info).await
            }
        }
    }

    // Extracts parameters information from available actions.
    fn get_action_parameters(&self, action_name: Option<&String>) -> Vec<Parameter> {
        action_name
            .and_then(|name| {
                self.available_actions
                    .iter()
                    .filter_map(|available_action| {
                        if &available_action.name == name {
                            Some(available_action.parameters.clone())
                        } else {
                            None
                        }
                    })
                    .next()
            })
            .unwrap_or(vec![])
    }

    // Handle Addmod suggestions.
    pub async fn issue_action(&mut self, add_mod: &AddModInfo, focus: bool) -> Result<(), Error> {
        self.execute_actions(vec![&add_mod], focus).await?;

        let mut output_consumers: Vec<OutputConsumer> = vec![];
        let action_parameters = self.get_action_parameters(add_mod.intent().action.as_ref());

        let context_store = self.story_context_store.lock();
        for param in add_mod.intent().parameters() {
            self.actions
                .entry(param.entity_reference.clone())
                .or_insert(HashSet::new())
                .insert(add_mod.clone());
            let consume_type = action_parameters
                .iter()
                .filter_map(|manifest_parameter| {
                    if &manifest_parameter.name == &param.name {
                        Some(manifest_parameter.parameter_type.as_str())
                    } else {
                        None
                    }
                })
                .next()
                .unwrap_or("None");
            output_consumers.append(&mut context_store.get_output_consumers(
                &param.entity_reference,
                add_mod.story_name(),
                consume_type,
            ));
        }
        let mut story_manager = self.story_manager.lock();
        story_manager.add_to_story_graph(&add_mod, output_consumers).await
    }

    // Handle restore story suggestions.
    async fn handle_restory_story(
        &mut self,
        restore_story_info: &RestoreStoryInfo,
    ) -> Result<(), Error> {
        let mut story_manager = self.story_manager.lock();
        let modules =
            story_manager.restore_story_graph(restore_story_info.story_name.to_string()).await?;

        let mut context_store = self.story_context_store.lock();
        context_store.restore_from_story(&modules, &restore_story_info.story_name).await?;

        for module in modules.iter() {
            for param in module.module_data.last_intent.parameters() {
                // Restore mod_manager.actions that record relationships
                // between consumers of entities.
                self.actions
                    .entry(param.entity_reference.clone())
                    .or_insert(HashSet::new())
                    .insert(AddModInfo::new(
                        module.module_data.last_intent.clone(),
                        Some(restore_story_info.story_name.clone()),
                        Some(module.module_id.to_string()),
                    ));
            }
        }

        let actions = modules
            .into_iter()
            .map(|module| {
                AddModInfo::new(
                    module.module_data.last_intent,
                    Some(restore_story_info.story_name.clone()),
                    Some(module.module_id),
                )
            })
            .collect::<Vec<AddModInfo>>();
        self.execute_actions(actions.iter().collect(), /*focus=*/ true).await
    }

    // This is called with story_context_store locked.
    pub async fn replace<'a>(
        &'a mut self,
        old: &'a str,
        new: &'a str,
        contributor: Contributor,
    ) -> Result<(), Error> {
        let actions = self
            .actions
            .remove(old)
            .unwrap_or(HashSet::new())
            .into_iter()
            .map(|action| action.replace_reference_in_parameters(old, new))
            .collect::<HashSet<AddModInfo>>();
        join_all(actions.iter().map(|action| self.execute_actions(vec![action], /*focus=*/ false)))
            .await;
        self.actions.insert(new.to_string(), actions);
        let mut story_manager = self.story_manager.lock();
        story_manager.update_graph_for_replace(old, new, contributor).await
    }

    async fn execute_actions<'a>(
        &'a self,
        actions: Vec<&'a AddModInfo>,
        focus: bool,
    ) -> Result<(), Error> {
        let (story_puppet_master, server_end) =
            fidl::endpoints::create_proxy::<StoryPuppetMasterMarker>()?;
        self.puppet_master.control_story(&actions[0].story_name(), server_end)?;
        let mut commands = actions
            .iter()
            .map(|&action| StoryCommand::AddMod(action.clone().into()))
            .collect::<Vec<StoryCommand>>();

        // TODO: story_manager to record the name of focused mod in a story.
        // Currently we just focus on the first mod in a story.
        if focus {
            commands.push(StoryCommand::SetFocusState(SetFocusState { focused: true }));
            commands.push(StoryCommand::FocusMod(FocusMod {
                mod_name: vec![],
                mod_name_transitional: Some(actions[0].mod_name.to_string()),
            }));
        }

        story_puppet_master.enqueue(&mut commands.iter_mut())?;
        let result = story_puppet_master.execute().await?;
        if result.status != ExecuteStatus::Ok {
            anyhow::bail!(
                "Modular error status:{:?} message:{}",
                result.status,
                result.error_message.unwrap_or("none".to_string())
            );
        } else {
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            models::{DisplayInfo, Intent},
            testing::{init_state, puppet_master_fake::PuppetMasterFake},
        },
        anyhow::Context as _,
        fidl_fuchsia_modular::{
            EntityResolverMarker, IntentParameter, IntentParameterData, PuppetMasterMarker,
        },
        fuchsia_async as fasync,
        fuchsia_component::client::connect_to_service,
        maplit::{hashmap, hashset},
    };

    // Tests that the right action is executed for a suggestion.
    // For this Puppet master is mocked and on a call to execute the command
    // will be verified. It also ensures that the suggestion action is tracked
    // and associated to the entity reference for future re-issuance.
    #[fasync::run_singlethreaded(test)]
    async fn execute_suggestion() -> Result<(), Error> {
        // Setup puppet master fake.
        let (puppet_master_client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();
        let mut puppet_master_fake = PuppetMasterFake::new();

        // This will be called when the suggestion is executed.
        puppet_master_fake.set_on_execute("story_name", |commands| {
            assert_eq!(commands.len(), 3);
            if let (
                StoryCommand::AddMod(add_mod),
                StoryCommand::SetFocusState(set_focus),
                StoryCommand::FocusMod(focus_mod),
            ) = (&commands[0], &commands[1], &commands[2])
            {
                assert_eq!(add_mod.intent.action, Some("PLAY_MUSIC".to_string()));
                assert_eq!(
                    add_mod.intent.parameters,
                    Some(vec![IntentParameter {
                        name: Some("artist".to_string()),
                        data: IntentParameterData::EntityReference("peridot-ref".to_string()),
                    },])
                );
                assert!(set_focus.focused);
                assert_eq!(focus_mod.mod_name_transitional, add_mod.mod_name_transitional);
            } else {
                assert!(false);
            }
        });

        let entity_resolver = connect_to_service::<EntityResolverMarker>()
            .context("failed to connect to entity resolver")?;
        puppet_master_fake.spawn(request_stream);
        let (_, _, mod_manager_arc) = init_state(puppet_master_client, entity_resolver);
        let mut mod_manager = mod_manager_arc.lock();

        let suggestion = suggestion!(
            action = "PLAY_MUSIC",
            title = "Play music",
            parameters = [(name = "artist", entity_reference = "peridot-ref")],
            story = "story_name"
        );
        match suggestion.action() {
            SuggestedAction::AddMod(action) => {
                let action_clone = action.clone();
                mod_manager.execute_suggestion(suggestion).await?;
                // Assert the action and the reference that originated it are tracked.
                assert_eq!(
                    mod_manager.actions,
                    hashmap!("peridot-ref".to_string() => hashset!(action_clone))
                );
            }
            SuggestedAction::RestoreStory(_) => {
                assert!(false);
            }
        }

        Ok(())
    }

    // Tests that stored actions are issued with the udpated entity reference
    // when replace is called. The old action will be tracked and associated to
    // the new reference.
    // For this Puppet master is mocked and on a call to execute the command
    // will be verified.
    #[fasync::run_singlethreaded(test)]
    async fn replace() -> Result<(), Error> {
        // Setup puppet master fake.
        let (puppet_master_client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();
        let mut puppet_master_fake = PuppetMasterFake::new();

        // This will be called with the action of the old reference but with
        // the replaced entity reference.
        puppet_master_fake.set_on_execute("story_name", |commands| {
            assert_eq!(commands.len(), 1);
            if let StoryCommand::AddMod(add_mod) = &commands[0] {
                assert_eq!(add_mod.intent.action, Some("PLAY_MUSIC".to_string()));
                assert_eq!(add_mod.mod_name_transitional, Some("mod_name".to_string()));
                assert_eq!(
                    add_mod.intent.parameters,
                    Some(vec![IntentParameter {
                        name: Some("artist".to_string()),
                        data: IntentParameterData::EntityReference("garnet-ref".to_string()),
                    },])
                );
            } else {
                assert!(false);
            }
        });

        let entity_resolver = connect_to_service::<EntityResolverMarker>()
            .context("failed to connect to entity resolver")?;
        puppet_master_fake.spawn(request_stream);
        let (_, _, mod_manager_arc) = init_state(puppet_master_client, entity_resolver);
        let mut mod_manager = mod_manager_arc.lock();

        // Set initial state. The actions here will be executed with the new
        // entity reference in the parameter.
        let intent = Intent::new().with_action("PLAY_MUSIC").add_parameter("artist", "peridot-ref");
        let action =
            AddModInfo::new(intent, Some("story_name".to_string()), Some("mod_name".to_string()));
        mod_manager.actions = hashmap!("peridot-ref".to_string() => hashset!(action));

        mod_manager
            .replace(
                "peridot-ref",
                "garnet-ref",
                Contributor::module_new("story_name", "mod_name", "artist"),
            )
            .await?;

        // Assert the action was replaced.
        let intent = Intent::new().with_action("PLAY_MUSIC").add_parameter("artist", "garnet-ref");
        let action =
            AddModInfo::new(intent, Some("story_name".to_string()), Some("mod_name".to_string()));
        assert_eq!(mod_manager.actions, hashmap!("garnet-ref".to_string() => hashset!(action)));

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn handle_restory_story() -> Result<(), Error> {
        // Setup puppet master fake.
        let (puppet_master_client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();
        let mut puppet_master_fake = PuppetMasterFake::new();
        puppet_master_fake.set_on_execute("story_name", |_commands| {});
        puppet_master_fake.spawn(request_stream);

        // Setup mod_manager.
        let entity_resolver = connect_to_service::<EntityResolverMarker>()
            .context("failed to connect to entity resolver")?;
        let (_, _, mod_manager_arc) = init_state(puppet_master_client, entity_resolver);
        let mut mod_manager = mod_manager_arc.lock();

        let suggestion = suggestion!(
            action = "PLAY_MUSIC",
            title = "Play music",
            parameters = [(name = "artist", entity_reference = "peridot-ref")],
            story = "story_name"
        );

        let mut action_clone = AddModInfo::new(Intent::new(), None, None);
        match suggestion.action() {
            SuggestedAction::AddMod(action) => {
                action_clone = action.clone();
                mod_manager.execute_suggestion(suggestion).await?;
            }
            SuggestedAction::RestoreStory(_) => {
                assert!(false);
            }
        }

        // Mock restarting the system and restoring a story.
        mod_manager.actions.clear();
        let restore_story_info = RestoreStoryInfo::new("story_name");
        mod_manager.handle_restory_story(&restore_story_info).await?;
        assert_eq!(
            mod_manager.actions,
            hashmap!("peridot-ref".to_string() => hashset!(action_clone))
        );

        Ok(())
    }
}
