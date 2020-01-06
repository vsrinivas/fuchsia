#include "src/lib/cobalt/cpp/project_profile.h"

#include "src/cobalt/bin/utils/base64.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/strings.h"

namespace cobalt {

fuchsia::cobalt::ProjectProfile ProjectProfileFromBase64String(const std::string &encoded_cfg) {
  std::string cfg = Base64Decode(encoded_cfg);

  return ProjectProfileFromString(cfg);
}

fuchsia::cobalt::ProjectProfile ProjectProfileFromString(const std::string &cfg) {
  fsl::SizedVmo config_vmo;
  bool success = fsl::VmoFromString(cfg, &config_vmo);
  FXL_CHECK(success) << "Could not convert Cobalt metrics registry string into VMO";

  return ProjectProfileFromVmo(std::move(config_vmo));
}

fuchsia::cobalt::ProjectProfile ProjectProfileFromFile(const std::string &filename) {
  fsl::SizedVmo config_vmo;
  bool success = fsl::VmoFromFilename(filename, &config_vmo);
  FXL_CHECK(success) << "Could not read Cobalt metrics registry file into VMO";

  return ProjectProfileFromVmo(std::move(config_vmo));
}

fuchsia::cobalt::ProjectProfile ProjectProfileFromVmo(fsl::SizedVmo vmo) {
  fuchsia::cobalt::ProjectProfile profile;
  profile.config = std::move(vmo).ToTransport();
  return profile;
}

}  // namespace cobalt
