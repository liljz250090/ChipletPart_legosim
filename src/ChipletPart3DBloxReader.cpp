#include "ChipletPart3DBloxReader.h"

#include <yaml-cpp/yaml.h>

#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace chiplet {
namespace {

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

std::string asString(const YAML::Node& node, const std::string& fallback = std::string())
{
  if (!node || node.IsNull()) {
    return fallback;
  }
  return node.as<std::string>();
}

std::string escapeXml(const std::string& text)
{
  std::string escaped;
  escaped.reserve(text.size());
  for (const char ch : text) {
    switch (ch) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&apos;";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

void writeXmlList(const std::filesystem::path& path,
                  const std::string& root_name,
                  const std::string& child_name,
                  const YAML::Node& items)
{
  std::ofstream out(path);
  if (!out.is_open()) {
    throw std::runtime_error("Could not open output file: " + path.string());
  }

  out << "<" << root_name << ">\n";
  if (items && items.IsSequence()) {
    for (const auto& item : items) {
      out << "  <" << child_name;
      if (item.IsMap()) {
        for (const auto& kv : item) {
          const auto key = kv.first.as<std::string>();
          const auto value = asString(kv.second);
          out << " " << key << "=\"" << escapeXml(value) << "\"";
        }
      }
      out << ">\n";
      out << "  </" << child_name << ">\n";
    }
  }
  out << "</" << root_name << ">\n";
}

YAML::Node loadDbxDataset(const std::string& dbx_file)
{
  const auto root = YAML::LoadFile(dbx_file);
  const auto dataset
      = root["Design"]["external"]["chipletpart_vendor_data"]["dataset"];
  if (!dataset || !dataset.IsMap()) {
    throw std::runtime_error(
        "3dblox file does not contain Design.external.chipletpart_vendor_data.dataset: "
        + dbx_file);
  }
  return dataset;
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

}  // namespace

IRDesign ChipletPart3DBloxReader::ReadDesign(const std::string& dbx_file,
                                             const std::string& dbv_file) const
{
  (void) dbv_file;
  const auto root = YAML::LoadFile(dbx_file);
  const auto dataset = loadDbxDataset(dbx_file);

  IRDesign design;
  design.name = asString(root["Design"]["name"], "3dblox_design");

  std::unordered_map<std::string, float> reach_by_type;
  if (const auto io_library = dataset["io_library"]; io_library && io_library.IsSequence()) {
    for (const auto& io : io_library) {
      const auto type = asString(io["type"]);
      if (type.empty()) {
        continue;
      }
      reach_by_type[type] = asFloat(io["reach"], -1.0f);
    }
  }

  std::unordered_map<std::string, std::pair<float, float>> locations;
  if (const auto stack = root["Stack"]; stack && stack.IsMap()) {
    for (const auto& entry : stack) {
      const auto name = entry.first.as<std::string>();
      const auto node = entry.second;
      if (const auto loc = node["loc"]; loc && loc.IsSequence() && loc.size() >= 2) {
        locations[name] = {asFloat(loc[0]), asFloat(loc[1])};
      }
    }
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

ChipletPartLegacyInputFiles ChipletPart3DBloxReader::MaterializeLegacyInputs(
    const std::string& dbx_file,
    const std::filesystem::path& output_dir) const
{
  const auto dataset = loadDbxDataset(dbx_file);
  std::filesystem::create_directories(output_dir);

  ChipletPartLegacyInputFiles files;
  files.base_dir = output_dir;
  files.io_file = (output_dir / "io_definitions.xml").string();
  files.layer_file = (output_dir / "layer_definitions.xml").string();
  files.wafer_process_file = (output_dir / "wafer_process_definitions.xml").string();
  files.assembly_process_file = (output_dir / "assembly_process_definitions.xml").string();
  files.test_file = (output_dir / "test_definitions.xml").string();
  files.netlist_file = (output_dir / "block_level_netlist.xml").string();
  files.blocks_file = (output_dir / "block_definitions.txt").string();

  writeXmlList(files.io_file, "ios", "io", dataset["io_library"]);
  writeXmlList(files.layer_file, "layers", "layer", dataset["layer_library"]);
  writeXmlList(files.wafer_process_file,
               "wafer_processes",
               "wafer_process",
               dataset["wafer_process_library"]);
  writeXmlList(files.assembly_process_file,
               "assembly_processes",
               "assembly",
               dataset["assembly_process_library"]);
  writeXmlList(files.test_file,
               "test_processes",
               "test_process",
               dataset["test_process_library"]);

  const auto referenced_block_order
      = buildReferencedBlockOrder(dataset["interconnects"]);

  {
    std::ofstream out(files.netlist_file);
    if (!out.is_open()) {
      throw std::runtime_error("Could not open output file: " + files.netlist_file);
    }
    out << "<netlist>\n";
    if (const auto interconnects = dataset["interconnects"];
        interconnects && interconnects.IsSequence()) {
      for (const auto& net : interconnects) {
        out << "  <net";
        out << " name=\"" << escapeXml(asString(net["name"])) << "\"";
        out << " type=\"" << escapeXml(asString(net["type"])) << "\"";
        out << " block0=\"" << escapeXml(asString(net["source"])) << "\"";
        out << " block1=\"" << escapeXml(asString(net["sink"])) << "\"";
        out << " bb_count=\"" << escapeXml(asString(net["bb_count"])) << "\"";
        out << " bandwidth=\"" << escapeXml(asString(net["bandwidth_gbps"])) << "\"";
        out << " average_bandwidth_utilization=\""
            << escapeXml(asString(net["average_bandwidth_utilization"], "0.5"))
            << "\">\n";
        out << "  </net>\n";
      }
    }
    out << "</netlist>\n";
  }

  {
    std::ofstream out(files.blocks_file);
    if (!out.is_open()) {
      throw std::runtime_error("Could not open output file: " + files.blocks_file);
    }
    if (const auto blocks = dataset["blocks"]; blocks && blocks.IsSequence()) {
      for (const auto& block : blocks) {
        const auto block_name = asString(block["name"]);
        if (block_name.empty()) {
          continue;
        }
        out << block_name << " " << asString(block["area_mm2"], "1.0")
          << " " << asString(block["power_w"], "0.0") << " "
          << asString(block["technology"], "unknown") << " "
          << (asBool(block["is_memory"], false) ? 1 : 0) << "\n";
      }
    }
  }

  return files;
}

}  // namespace chiplet
