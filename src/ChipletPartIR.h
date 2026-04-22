#pragma once

#include <string>
#include <vector>

namespace chiplet {

struct IRBlock {
  std::string name;
  float area = 1.0f;
  float power = 0.0f;
  std::string tech;
  bool is_memory = false;
  bool is_port = false;
  float location_x = 0.0f;
  float location_y = 0.0f;
};

struct IRNet {
  std::string name;
  std::string type;
  std::vector<std::string> pins;
  float weight = 1.0f;
  float average_bandwidth_utilization = 0.5f;
  float reach = -1.0f;
  float io_size = 1.0f;
};

struct IRDesign {
  std::string name;
  std::vector<IRBlock> blocks;
  std::vector<IRNet> nets;
};

}  // namespace chiplet
