const RECONNECT_INTERVAL = 500;
const MAX_RECONNECT_INTERVAL = 2000;

var _webSocket = null;
var _reconnectInterval = RECONNECT_INTERVAL;

$(function() {
  connectWebSocket();
})

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
  if ("action_log.all" in message) {
    handleActionLogReset(message["action_log.all"]);
  }
  if ("action_log.new_action" in message) {
    handleActionLogAdd(message["action_log.new_action"]);
  }
}

function handleContextUpdate(context) {
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
        .append($("<td/>").text(topic))
        .append($("<td/>").attr("id", topicId)
          .append(($("<pre/>").text(topicValue)
            .toggleClass("text-danger", danger))));
    }
  });
}

function handleContextSubscribers(subscribers) {
  $("#contextSubscriptions").empty().append(subscribers.map(function(update) {
    var component = $("<td/>").append($("<span/>").addClass("text-capitalize")
      .text(update.subscriber.type));
    if (update.subscriber.url) {
      component.append(" ").append($("<span/>").text(update.subscriber.url));
    }
    if (update.subscriber.storyId) {
      component.append(" ").append($("<span/>")
        .text("from story " + update.subscriber.storyId + ")"));
    }

    var queries = $("<td/>").append(update.queries.map(function(query) {
      return $("<div/>").addClass("context-query")
        .append(query.length == 0? "(all topics)" :
          query.map(function(topic) {
            return $("<span/>").addClass("context-topic").text(topic);
          }));
    }));

    return $("<tr/>")
      .append(component)
      .append(queries);
  }));
}

function handleActionLogReset(actionList) {
  $("#actionLog").empty();

  actionList.forEach(handleActionLogAdd);
}

function handleActionLogAdd(action) {
  var methodElem = $("<td/>").text(action.method);
  var componentUrlElem = $("<td/>").text(action.component_url);
  var parametersElem = $("<td/>").text(action.parameters);

  $("#actionLog").prepend($("<tr/>").append(methodElem)
                                    .append(componentUrlElem)
                                    .append(parametersElem));
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
