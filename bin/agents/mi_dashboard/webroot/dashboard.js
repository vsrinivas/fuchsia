const RECONNECT_INTERVAL = 500;
const MAX_RECONNECT_INTERVAL = 2000;

var _webSocket = null;
var _reconnectInterval = RECONNECT_INTERVAL;
var _focusedStoryId = null;
var _stories = {};
var _toolbar = null;
var _tabBar = null;

$(function() {
  mdc.autoInit();
  _toolbar =
    new mdc.toolbar.MDCToolbar(document.querySelector('.mdc-toolbar'));
  _tabBar =
    new mdc.tabs.MDCTabBar(document.querySelector('#dashboard-tab-bar'));

  _tabBar.listen('MDCTabBar:change', function (t) {
      var newPanelSelector = _tabBar.activeTab.root_.hash;
      updateTabPanel(newPanelSelector);
    });

  _tabBar.layout();

  connectWebSocket();
})

function updateTabPanel(newPanelSelector) {
  var activePanel = document.querySelector('.panel.active');
  if (activePanel) {
    activePanel.classList.remove('active');
  }
  var newActivePanel = document.querySelector(newPanelSelector);
  if (newActivePanel) {
    newActivePanel.classList.add('active');
  }
}

function connectWebSocket() {
  _webSocket = new WebSocket("ws://" + window.location.host + "/ws");
  _webSocket.onopen = handleWebSocketOpen;
  _webSocket.onerror = handleWebSocketError;
  _webSocket.onclose = handleWebSocketClose;
  _webSocket.onmessage = handleWebSocketMessage;
}

function handleWebSocketOpen(evt) {
  $("#connectedLabel").text("Connected");
  // reset reconnect
  _reconnectInterval = RECONNECT_INTERVAL;
}

function handleWebSocketError(evt) {
  console.log("WebSocket Error: " + evt.toString());
}

function handleWebSocketClose(evt) {
  $("#connectedLabel").text("Disconnected");

  // attempt to reconnect
  attemptReconnect();
}

function handleWebSocketMessage(evt) {
  // parse the JSON message
  var message = JSON.parse(evt.data);
  if ("context.update" in message) {
    handleContextUpdate(message["context.update"]);
  }
  if ("context.subscribers" in message) {
    handleContextSubscribers(message["context.subscribers"]);
  }
  if ("suggestions" in message) {
    handleSuggestionsUpdate(message["suggestions"]);
  }
  if ("action_log.all" in message) {
    handleActionLogReset(message["action_log.all"]);
  }
  if ("action_log.new_action" in message) {
    handleActionLogAdd(message["action_log.new_action"]);
  }
}

function handleSuggestionUpdate(suggestions) {
  console.log('Got suggestions:', suggestions);
}

function handleContextUpdate(context) {
  updateOverview(context);

  $.each(context, function(topic, rawValue) {
    // make a pretty string for the topic's value
    var topicValue;
    var danger;
    try {
      topicValue = JSON.stringify(JSON.parse(rawValue), null, 2);
      danger = false; // can't just leave it undefined;
                      // .toggleClass has a 1-arg overload
    } catch (e) {
      topicValue = rawValue;
      danger = true;
    }
    var topicId = "#topic-" + topic;
    var existingTopic = $("#" + $.escapeSelector(topicId));
    if (existingTopic.length > 0) {
      // element exists, update the value
      existingTopic.find("pre").text(topicValue)
        .toggleClass("text-danger", danger);
    } else {
      // element does not exist, add it to the table
      $("<tr/>").appendTo("#context")
        .append($("<td/>").addClass('wrappable').text(topic))
        .append($("<td/>").attr("id", topicId)
          .append(($("<pre/>").text(topicValue)
            .toggleClass("text-danger", danger))));
    }
  });
}

function handleContextSubscribers(subscribers) {
  $("#contextSubscriptions").empty();

  subscribers.forEach(function(update) {
    // Sample HTML to be created
    // <li class="mdc-list-item">
    //   <span class="mdc-list-item__text">
    //     <span class="agent-url">file:///system/apps/agents/module_suggester</span>
    //     <span class="mdc-list-item__text__secondary">raw/text</span>
    //   </span>
    // </li>
    // <li class="mdc-list-divider" role="separator"></li>
    var listText = $('<span/>').addClass('mdc-list-item__text');

    var agentNameText = update.subscriber.type;
    if (update.subscriber.url) {
      agentNameText += ' ' + update.subscriber.url;
    }
    if (update.subscriber.storyId) {
      agentNameText += ' from story ' + update.subscriber.storyId;
    }

    listText.append($("<span/>").addClass("subscriber")
                                .text(agentNameText));
    listText.append(' ').append(update.queries.map(function(query) {
      var querySpan = $('<span/>')
        .addClass('mdc-list-item__text__secondary')
        .addClass('context-topic');
      if (query.length == 0) {
        querySpan.append($('<span/>').text('(all topics)'));
      } else {
        query.forEach(function(topic) {
          querySpan.append($('<span/>').text(topic))
            .append(' ');
        })
      }
      return querySpan;
    }));

    $("#contextSubscriptions")
      .append($('<li/>').addClass('mdc-list-item').append(listText))
      .append($('<li/>').addClass('mdc-list-divider').attr('role','divider'));
  });
}

function handleActionLogReset(actionList) {
  $("#actionLog").empty();

  actionList.forEach(handleActionLogAdd);
}

function handleActionLogAdd(action) {
  // <li class="mdc-list-item">
  //   <span class="mdc-list-item__text">
  //     Two-line item
  //     <span class="mdc-list-item__text__secondary">Secondary text</span>
  //   </span>
  // </li>

  // Assemble Action Log Panel Data
  var methodElem = $("<span/>")
    .addClass('mdc-list-item__text')
    .text(action.method);
  var componentUrlElem = $("<span/>")
    .addClass('mdc-list-item__text__secondary')
    .text(action.component_url);
  var parametersElem = $("<span/>")
    .addClass('mdc-list-item__text__secondary')
    .text(action.parameters);

  methodElem.append(' ')
    .append(componentUrlElem)
    .append(' ')
    .append(parametersElem);

  $("#actionLog")
    .prepend($('<li/>').addClass('mdc-list-item').append(methodElem))
    .prepend($('<li/>').addClass('mdc-list-divider').attr('role','divider'));

  // Assemble Action Log Overview Data
  methodElem = $('<span/>')
    .addClass('mdc-list-item__text')
    .text(action.method);
  parametersElem = $("<span/>")
    .addClass('mdc-list-item__text__secondary')
    .text(action.parameters);

  methodElem.append(" ").append(parametersElem);
  $('#actionLogOverview')
    .prepend($('<li/>').addClass('mdc-list-item').append(methodElem))
    .prepend($('<li/>').addClass('mdc-list-divider').attr('role','divider'));
}

function attemptReconnect() {
  console.log("Attempting to reconnect after " + _reconnectInterval);

  // reconnect after the timeout
  setTimeout(connectWebSocket, _reconnectInterval);
  // exponential reconnect timeout
  var nextInterval = _reconnectInterval * 2;
  if (nextInterval < MAX_RECONNECT_INTERVAL) {
    _reconnectInterval = nextInterval;
  } else {
    _reconnectInterval = MAX_RECONNECT_INTERVAL;
  }
}

function updateOverview(context) {
  $.each(context, function(topic, rawValue) {
    // loop through the context updates and modify anything related
    // on the overview panel
    var storyRegex = /\/story\/id\/([^\/]+)\/(.+)/;
    var storyRegexResults = topic.match(storyRegex);

    if (storyRegexResults != null && storyRegexResults[1] != null) {
      // this is a story-related topic
      var storyId = storyRegexResults[1];
      var overviewStoryElems = getOrCreateStoryOverviewElements(storyId);

      switch(storyRegexResults[2]) {
        case "url":
          setStoryName(rawValue, overviewStoryElems);
          break;

        case "state":
          setStoryState(rawValue, overviewStoryElems);
          break;
      }

      // move this story to the top of the list
      // TODO(jwnichols): Figure out the right thing to do here
      var divider = overviewStoryElems.prev();
      $('#story-overview-list')
        .prepend(overviewStoryElems)
        .prepend(divider);
    } else if (topic == '/story/focused_id') {
      if (_focusedStoryId != null) {
        var oldFocusedStoryElems = getOrCreateStoryOverviewElements(_focusedStoryId);
        setStoryVisible(false, oldFocusedStoryElems);
      }
      var newFocusedStoryId = JSON.parse(rawValue);
      if (newFocusedStoryId != null) {
        var newFocusedStoryElems = getOrCreateStoryOverviewElements(newFocusedStoryId);
        setStoryVisible(true, newFocusedStoryElems);
      }
      _focusedStoryId = newFocusedStoryId;
    } else if (topic == '/suggestion_engine/current_query') {
      var query = rawValue;
      if (query.length == 0) {
        $('#askQueryOverview').empty().append($('<i/>').text('No Query'));
      } else {
        $('#askQueryOverview').empty().text(query);
      }
    }
  });
}

function getOrCreateStoryOverviewElements(storyId) {
  var storyElems = _stories[storyId];
  if (storyElems == null) {
    // we need to create the story elements
    // <li id="b234kj2jn5j34342l3k3" class="mdc-list-item story-list-item story-visible">
    //   <span class="story-name mdc-list-item__text">
    //     Story Name
    //     <span class="story-id mdc-list-item__text__secondary">
    //       b234kj2jn5j34342l3k3
    //     </span>
    //   </span>
    // </li>
    var storyListItem = $('<li/>').attr('id',storyId)
      .addClass('mdc-list-item')
      .addClass('story-list-item');

    var storyIdElem = $('<span/>').addClass('story-id')
      .addClass('mdc-list-item__text__secondary')
      .text(storyId);

    var storyNameElem = $('<span/>').addClass('story-name')
      .addClass('mdc-list-item__text')
      .text('Story Name')
      .append(' ')
      .append(storyIdElem);

    storyListItem.append(storyNameElem);

    $('#story-overview-list')
      .append($('<li/>').addClass('mdc-list-divider').attr('role','divider'))
      .append(storyListItem);

    storyElems = _stories[storyId] = storyListItem;
  }
  return storyElems;
}

function setStoryName(storyName, storyElems) {
  storyElems.find('span')[0].firstChild.textContent = storyName;
}

function setStoryVisible(storyVisible, storyElems) {
  if (storyVisible) {
    storyElems.addClass('story-visible');
  } else {
    storyElems.removeClass('story-visible');
  }
}

function setStoryState(storyState, storyElems) {
  // TODO(jwnichols): Add story state to the overview
}
