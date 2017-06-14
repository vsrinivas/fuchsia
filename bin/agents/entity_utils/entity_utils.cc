#include "apps/maxwell/src/agents/entity_utils/entity_utils.h"

#include "apps/maxwell/src/agents/entity_utils/entity_span.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "lib/ftl/logging.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace maxwell {

bool KeyInUpdateResult(const ContextUpdatePtr& result, const std::string& key) {
  // TODO(travismart): Currently, when we lose focus, several values become null
  // but this isn't reflected in their "is_null" methods. The string comparison,
  // below, however, does work.
  const bool ret = result->values.find(key) != result->values.end()
      && !result->values[key].is_null() && result->values[key] != "null";
  return ret;
}

}  // namespace maxwell
