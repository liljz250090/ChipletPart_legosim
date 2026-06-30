#include "ChipletPartOpenDBReader.h"

#include <yaml-cpp/yaml.h>

#include <stdexcept>

#if defined(CHIPLETPART_ENABLE_OPENDB_BACKEND)
#include "odb/db.h"
#include "odb/dbObject.h"
#include "odb/geom.h"
#include <set>
#include <unordered_map>
#include <unordered_set>
#endif

namespace chiplet {

namespace {

#if defined(CHIPLETPART_ENABLE_OPENDB_BACKEND)

float asFloat(const YAML::Node& node, float fallback = 0.0f)
{
  if (!node || node.IsNull()) {
    return fallback;
  }
  return node.as<float>();
}

bool asBool(const YAML::Node& node, bool fallback = false)
{
  if (!node || node.IsNull()) {
    return fallback;
  }
  return node.as<bool>();
}

std::string asString(const YAML::Node& node,
                     const std::string& fallback = std::string())
{
  if (!node || node.IsNull()) {
    return fallback;
  }
  return node.as<std::string>();
}

std::vector<std::string> buildReferencedBlockOrder(const YAML::Node& interconnects)
{
  std::vector<std::string> ordered_blocks;
  std::set<std::string> seen_blocks;
  if (!interconnects || !interconnects.IsSequence()) {
    return ordered_blocks;
  }

  for (const auto& net_node : interconnects) {
    for (const char* endpoint_key : {"source", "sink"}) {
      const auto endpoint = asString(net_node[endpoint_key]);
      if (endpoint.empty() || seen_blocks.contains(endpoint)) {
        continue;
      }
      seen_blocks.insert(endpoint);
      ordered_blocks.push_back(endpoint);
    }
  }

  return ordered_blocks;
}

IRDesign readDesignFromBlock(odb::dbBlock* block)
{
  IRDesign design;
  design.name = block->getName();

  std::unordered_map<std::string, std::size_t> block_index;
  auto ensure_block = [&](const IRBlock& candidate) {
    const auto it = block_index.find(candidate.name);
    if (it != block_index.end()) {
      return it->second;
    }
    const std::size_t index = design.blocks.size();
    design.blocks.push_back(candidate);
    block_index[candidate.name] = index;
    return index;
  };

  for (auto* term : block->getBTerms()) {
    IRBlock ir_block;
    ir_block.name = "__port__/" + term->getName();
    ir_block.area = 0.0f;
    ir_block.is_port = true;
    ensure_block(ir_block);
  }

  for (auto* inst : block->getInsts()) {
    auto* master = inst->getMaster();
    if (master == nullptr || master->isPad() || master->isCover()) {
      continue;
    }

    IRBlock ir_block;
    ir_block.name = inst->getName();
    if (auto* bbox = inst->getBBox()) {
      const odb::Rect rect = bbox->getBox();
      const double width_um = block->dbuToMicrons(rect.dx());
      const double height_um = block->dbuToMicrons(rect.dy());
      ir_block.area = static_cast<float>(width_um * height_um);
      ir_block.location_x = static_cast<float>(
          block->dbuToMicrons((rect.xMin() + rect.xMax()) / 2.0));
      ir_block.location_y = static_cast<float>(
          block->dbuToMicrons((rect.yMin() + rect.yMax()) / 2.0));
    } else {
      ir_block.area = 0.0f;
    }

    if (auto* lib = master->getLib()) {
      if (auto* tech = lib->getTech()) {
        ir_block.tech = tech->getName();
      }
    }

    ensure_block(ir_block);
  }

  for (auto* net : block->getNets()) {
    if (net->getSigType().isSupply()) {
      continue;
    }

    IRNet ir_net;
    ir_net.name = net->getName();
    ir_net.weight = 1.0f;
    ir_net.average_bandwidth_utilization = 0.5f;
    ir_net.io_size = 1.0f;
    ir_net.reach = -1.0f;

    std::unordered_set<std::string> seen_pins;

    for (auto* iterm : net->getITerms()) {
      auto* inst = iterm->getInst();
      auto* master = inst != nullptr ? inst->getMaster() : nullptr;
      if (inst == nullptr || master == nullptr || master->isPad()
          || master->isCover()) {
        continue;
      }

      const std::string inst_name = inst->getName();
      if (seen_pins.insert(inst_name).second) {
        ir_net.pins.push_back(inst_name);
      }
    }

    for (auto* bterm : net->getBTerms()) {
      const std::string term_name = "__port__/" + bterm->getName();
      if (seen_pins.insert(term_name).second) {
        ir_net.pins.push_back(term_name);
      }
    }

    if (ir_net.pins.size() >= 2) {
      design.nets.push_back(ir_net);
    }
  }

  return design;
}

IRDesign readDesignFromChip(odb::dbChip* chip)
{
  auto* dataset_prop
      = odb::dbStringProperty::find(chip, "chipletpart.3dbx.design_external_yaml");
  if (dataset_prop == nullptr) {
    throw std::runtime_error(
        "Top dbChip does not contain chipletpart.3dbx.design_external_yaml");
  }

  const YAML::Node design_external = YAML::Load(dataset_prop->getValue());
  const YAML::Node dataset
      = design_external["chipletpart_vendor_data"]["dataset"];
  if (!dataset || !dataset.IsMap()) {
    throw std::runtime_error(
        "ODB top-chip property does not contain chipletpart_vendor_data.dataset");
  }

  IRDesign design;
  design.name = chip->getName();

  std::unordered_map<std::string, float> reach_by_type;
  if (const auto io_library = dataset["io_library"];
      io_library && io_library.IsSequence()) {
    for (const auto& io : io_library) {
      const auto type = asString(io["type"]);
      if (type.empty()) {
        continue;
      }
      reach_by_type[type] = asFloat(io["reach"], -1.0f);
    }
  }

  std::unordered_map<std::string, std::pair<float, float>> locations;
  for (auto* chip_inst : chip->getChipInsts()) {
    const auto loc = chip_inst->getLoc();
    locations[chip_inst->getName()]
        = {static_cast<float>(loc.x()), static_cast<float>(loc.y())};
  }

  const auto referenced_block_order
      = buildReferencedBlockOrder(dataset["interconnects"]);

  std::unordered_map<std::string, IRBlock> block_definitions;
  if (const auto blocks = dataset["blocks"]; blocks && blocks.IsSequence()) {
    for (const auto& block_node : blocks) {
      IRBlock block;
      block.name = asString(block_node["name"]);
      if (block.name.empty()) {
        continue;
      }
      block.area = asFloat(block_node["area_mm2"], 1.0f);
      block.power = asFloat(block_node["power_w"], 0.0f);
      block.tech = asString(block_node["technology"]);
      block.is_memory = asBool(block_node["is_memory"], false);
      const auto loc_it = locations.find(block.name);
      if (loc_it != locations.end()) {
        block.location_x = loc_it->second.first;
        block.location_y = loc_it->second.second;
      }
      block_definitions[block.name] = block;
    }
  }

  std::unordered_map<std::string, IRBlock> block_map;
  for (const auto& block_name : referenced_block_order) {
    auto block_it = block_definitions.find(block_name);
    IRBlock block;
    if (block_it != block_definitions.end()) {
      block = block_it->second;
    } else {
      block.name = block_name;
    }
    design.blocks.push_back(block);
    block_map.emplace(block.name, block);
  }

  if (const auto interconnects = dataset["interconnects"];
      interconnects && interconnects.IsSequence()) {
    for (const auto& net_node : interconnects) {
      IRNet net;
      net.name = asString(net_node["name"]);
      net.type = asString(net_node["type"]);
      net.weight = asFloat(net_node["bandwidth_gbps"], 1.0f);
      net.average_bandwidth_utilization
          = asFloat(net_node["average_bandwidth_utilization"], 0.5f);
      net.io_size = 1.0f;
      const auto reach_it = reach_by_type.find(net.type);
      net.reach = (reach_it != reach_by_type.end()) ? reach_it->second : -1.0f;

      const auto source = asString(net_node["source"]);
      const auto sink = asString(net_node["sink"]);
      if (!source.empty()) {
        net.pins.push_back(source);
        if (block_map.find(source) == block_map.end()) {
          IRBlock block;
          block.name = source;
          design.blocks.push_back(block);
          block_map.emplace(source, block);
        }
      }
      if (!sink.empty()) {
        net.pins.push_back(sink);
        if (block_map.find(sink) == block_map.end()) {
          IRBlock block;
          block.name = sink;
          design.blocks.push_back(block);
          block_map.emplace(sink, block);
        }
      }

      if (net.pins.size() >= 2) {
        design.nets.push_back(net);
      }
    }
  }

  return design;
}

#endif

}  // namespace

IRDesign ChipletPartOpenDBReader::ReadDesign(void* db_block_handle) const
{
#if !defined(CHIPLETPART_ENABLE_OPENDB_BACKEND)
  (void) db_block_handle;
  throw std::runtime_error(
      "OpenDB backend is compiled out in this standalone build. "
      "Enable CHIPLETPART_ENABLE_OPENDB_BACKEND when building inside the "
      "OpenROAD/OpenDB environment.");
#else
  if (db_block_handle == nullptr) {
    throw std::invalid_argument("OpenDB input handle is null");
  }

  auto* object = reinterpret_cast<odb::dbObject*>(db_block_handle);
  const auto object_type = object->getObjectType();
  if (object_type == odb::dbBlockObj) {
    auto* block = reinterpret_cast<odb::dbBlock*>(db_block_handle);
    return readDesignFromBlock(block);
  }

  if (object_type == odb::dbChipObj) {
    auto* chip = reinterpret_cast<odb::dbChip*>(db_block_handle);
    return readDesignFromChip(chip);
  }

  throw std::runtime_error(std::string("Unsupported OpenDB input object type: ")
                           + object->getTypeName());
#endif
}

}  // namespace chiplet
