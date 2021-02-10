// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "OrbitBase/Logging.h"

namespace orbit_base {

std::optional<std::string> ExecuteCommand(const std::string& cmd) {
  std::unique_ptr<FILE, decltype(&pclose)> pipe{popen(cmd.c_str(), "r"), pclose};
  if (!pipe) {
    ERROR("Could not open pipe for \"%s\"", cmd.c_str());
    return std::optional<std::string>{};
  }

  std::array<char, 128> buffer;
  std::string result;
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  return result;
}

}  // namespace orbit_base
