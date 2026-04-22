#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <yaml-cpp/yaml.h>

#include "odb/db.h"

namespace {

constexpr const char* kTopDbxExternalProp = "chipletpart.3dbx.design_external_yaml";
constexpr const char* kDbvExternalProp = "chipletpart.3dbv.external_yaml";
constexpr const char* kInstDbxExternalProp = "chipletpart.3dbx.instance_external_yaml";
constexpr const char* kDbxSourceProp = "chipletpart.3dbx.source_file";
constexpr const char* kDbvSourceProp = "chipletpart.3dbv.source_file";

bool hasContent(const YAML::Node& node)
{
  if (!node || node.IsNull()) {
    return false;
  }
  if (node.IsScalar()) {
    return !node.as<std::string>().empty();
  }
  if (node.IsSequence() || node.IsMap()) {
    return node.size() > 0;
  }
  return false;
}

std::string emitYaml(const YAML::Node& node)
{
  YAML::Emitter out;
  out << node;
  if (!out.good()) {
    throw std::runtime_error("Failed to serialize YAML node");
  }
  return out.c_str();
}

void setStringProperty(odb::dbObject* object,
                       const char* name,
                       const std::string& value)
{
  if (auto* prop = odb::dbStringProperty::find(object, name)) {
    prop->setValue(value.c_str());
    return;
  }
  odb::dbStringProperty::create(object, name, value.c_str());
}

odb::dbDatabase* loadDb(const std::filesystem::path& path)
{
  auto* db = odb::dbDatabase::create();
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("Failed to open ODB file for reading: "
                             + path.string());
  }
  db->read(stream);
  return db;
}

void writeDb(odb::dbDatabase* db, const std::filesystem::path& path)
{
  std::ofstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("Failed to open ODB file for writing: "
                             + path.string());
  }
  db->write(stream);
}

void annotateDbx(odb::dbChip* top_chip,
                 const std::filesystem::path& dbx_file,
                 int& top_chip_props,
                 int& inst_props)
{
  const YAML::Node root = YAML::LoadFile(dbx_file.string());
  const YAML::Node design = root["Design"];
  const YAML::Node design_external = design["external"];
  if (hasContent(design_external)) {
    setStringProperty(top_chip, kTopDbxExternalProp, emitYaml(design_external));
    ++top_chip_props;
  }
  setStringProperty(top_chip, kDbxSourceProp, dbx_file.string());

  const YAML::Node chiplet_insts = root["ChipletInst"];
  if (!chiplet_insts || !chiplet_insts.IsMap()) {
    return;
  }

  for (const auto& entry : chiplet_insts) {
    const std::string inst_name = entry.first.as<std::string>();
    const YAML::Node inst_node = entry.second;
    const YAML::Node inst_external = inst_node["external"];
    if (!hasContent(inst_external)) {
      continue;
    }

    auto* chip_inst = top_chip->findChipInst(inst_name);
    if (chip_inst == nullptr) {
      continue;
    }
    setStringProperty(chip_inst, kInstDbxExternalProp, emitYaml(inst_external));
    ++inst_props;
  }
}

void annotateDbv(odb::dbDatabase* db,
                 const std::filesystem::path& dbv_file,
                 int& chip_props)
{
  const YAML::Node root = YAML::LoadFile(dbv_file.string());
  const YAML::Node chiplet_defs = root["ChipletDef"];
  if (!chiplet_defs || !chiplet_defs.IsMap()) {
    return;
  }

  for (const auto& entry : chiplet_defs) {
    const std::string chip_name = entry.first.as<std::string>();
    const YAML::Node chip_node = entry.second;
    const YAML::Node external = chip_node["external"];
    if (!hasContent(external)) {
      continue;
    }

    auto* chip = db->findChip(chip_name.c_str());
    if (chip == nullptr) {
      continue;
    }

    setStringProperty(chip, kDbvExternalProp, emitYaml(external));
    setStringProperty(chip, kDbvSourceProp, dbv_file.string());
    ++chip_props;
  }
}

}  // namespace

int main(int argc, char* argv[])
{
  if (argc != 5) {
    std::cerr << "Usage: " << argv[0]
              << " <input.odb> <source.3dbx> <source.3dbv> <output.odb>\n";
    return 1;
  }

  try {
    const std::filesystem::path odb_in = argv[1];
    const std::filesystem::path dbx_in = argv[2];
    const std::filesystem::path dbv_in = argv[3];
    const std::filesystem::path odb_out = argv[4];

    odb::dbDatabase* db = loadDb(odb_in);
    odb::dbChip* top_chip = db != nullptr ? db->getChip() : nullptr;
    if (top_chip == nullptr) {
      throw std::runtime_error("ODB does not contain a top chip: "
                               + odb_in.string());
    }

    int top_chip_props = 0;
    int inst_props = 0;
    int chip_props = 0;

    annotateDbx(top_chip, dbx_in, top_chip_props, inst_props);
    annotateDbv(db, dbv_in, chip_props);
    writeDb(db, odb_out);

    std::cout << "Annotated ODB written to " << odb_out << "\n";
    std::cout << "Top-chip external properties written: " << top_chip_props
              << "\n";
    std::cout << "Chip instance external properties written: " << inst_props
              << "\n";
    std::cout << "Chip definition external properties written: " << chip_props
              << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
