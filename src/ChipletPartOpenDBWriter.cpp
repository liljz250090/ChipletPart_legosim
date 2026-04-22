#include "ChipletPartOpenDBWriter.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(CHIPLETPART_ENABLE_OPENDB_BACKEND)
#include "odb/db.h"
#endif

namespace chiplet {

namespace {

constexpr const char* kPartitionIdProp = "chipletpart_partition_id";
constexpr const char* kPartitionNameProp = "chipletpart_partition_name";
constexpr const char* kPartitionCountProp = "chipletpart_partition_count";
constexpr const char* kGroupPrefix = "chipletpart_partition_";
constexpr const char* kPortPrefix = "__port__/";

#if defined(CHIPLETPART_ENABLE_OPENDB_BACKEND)
void SetIntProperty(odb::dbObject* object, const char* name, int value)
{
  if (auto* prop = odb::dbIntProperty::find(object, name)) {
    prop->setValue(value);
    return;
  }
  odb::dbIntProperty::create(object, name, value);
}

void SetStringProperty(odb::dbObject* object,
                       const char* name,
                       const std::string& value)
{
  if (auto* prop = odb::dbStringProperty::find(object, name)) {
    prop->setValue(value.c_str());
    return;
  }
  odb::dbStringProperty::create(object, name, value.c_str());
}
#endif

}  // namespace

OpenDBWriteStats ChipletPartOpenDBWriter::WritePartition(
    void* db_block_handle,
    const std::vector<int>& solution,
    const std::unordered_map<int, std::string>& vertex_index_to_name) const
{
#if !defined(CHIPLETPART_ENABLE_OPENDB_BACKEND)
  (void) db_block_handle;
  (void) solution;
  (void) vertex_index_to_name;
  throw std::runtime_error(
      "OpenDB backend is compiled out in this standalone build. "
      "Enable CHIPLETPART_ENABLE_OPENDB_BACKEND when building inside the "
      "OpenROAD/OpenDB environment.");
#else
  if (db_block_handle == nullptr) {
    throw std::invalid_argument("OpenDB output handle is null");
  }

  OpenDBWriteStats stats;
  auto* object = reinterpret_cast<odb::dbObject*>(db_block_handle);
  if (solution.empty()) {
    SetIntProperty(object, kPartitionCountProp, 0);
    return stats;
  }

  int max_partition_id = -1;
  for (const int part_id : solution) {
    max_partition_id = std::max(max_partition_id, part_id);
  }
  stats.partition_count = max_partition_id + 1;

  if (object->getObjectType() == odb::dbBlockObj) {
    auto* block = reinterpret_cast<odb::dbBlock*>(db_block_handle);
    std::vector<odb::dbGroup*> stale_groups;
    for (auto* group : block->getGroups()) {
      const std::string group_name = group->getName();
      if (group_name.rfind(kGroupPrefix, 0) == 0) {
        stale_groups.push_back(group);
      }
    }
    for (auto* group : stale_groups) {
      odb::dbGroup::destroy(group);
    }

    std::unordered_map<int, odb::dbGroup*> groups;
    for (int vertex_id = 0; vertex_id < static_cast<int>(solution.size()); ++vertex_id) {
      const auto vertex_name_it = vertex_index_to_name.find(vertex_id);
      if (vertex_name_it == vertex_index_to_name.end()) {
        ++stats.missing_vertices;
        continue;
      }

      const int part_id = solution[vertex_id];
      const std::string part_name = std::string("chiplet_") + std::to_string(part_id);
      const std::string& vertex_name = vertex_name_it->second;

      if (vertex_name.rfind(kPortPrefix, 0) == 0) {
        const std::string port_name = vertex_name.substr(std::char_traits<char>::length(kPortPrefix));
        if (auto* bterm = block->findBTerm(port_name.c_str())) {
          SetIntProperty(bterm, kPartitionIdProp, part_id);
          ++stats.bterms_written;
        } else {
          ++stats.missing_bterms;
        }
        continue;
      }

      auto* inst = block->findInst(vertex_name.c_str());
      if (inst == nullptr) {
        ++stats.missing_insts;
        continue;
      }

      SetIntProperty(inst, kPartitionIdProp, part_id);
      SetStringProperty(inst, kPartitionNameProp, part_name);
      ++stats.insts_written;

      auto group_it = groups.find(part_id);
      if (group_it == groups.end()) {
        const std::string group_name = std::string(kGroupPrefix) + std::to_string(part_id);
        auto* group = odb::dbGroup::create(block, group_name.c_str());
        groups[part_id] = group;
        group_it = groups.find(part_id);
        ++stats.groups_created;
      }
      group_it->second->addInst(inst);
    }

    SetIntProperty(block, kPartitionCountProp, stats.partition_count);
    return stats;
  }

  if (object->getObjectType() == odb::dbChipObj) {
    auto* chip = reinterpret_cast<odb::dbChip*>(db_block_handle);
    for (int vertex_id = 0; vertex_id < static_cast<int>(solution.size()); ++vertex_id) {
      const auto vertex_name_it = vertex_index_to_name.find(vertex_id);
      if (vertex_name_it == vertex_index_to_name.end()) {
        ++stats.missing_vertices;
        continue;
      }

      const int part_id = solution[vertex_id];
      const std::string part_name = std::string("chiplet_") + std::to_string(part_id);
      const std::string& vertex_name = vertex_name_it->second;

      auto* chip_inst = chip->findChipInst(vertex_name);
      if (chip_inst == nullptr) {
        ++stats.missing_insts;
        continue;
      }

      SetIntProperty(chip_inst, kPartitionIdProp, part_id);
      SetStringProperty(chip_inst, kPartitionNameProp, part_name);
      ++stats.insts_written;
    }

    SetIntProperty(chip, kPartitionCountProp, stats.partition_count);
    return stats;
  }

  throw std::runtime_error(std::string("Unsupported OpenDB writeback object type: ")
                           + object->getTypeName());
#endif
}

}  // namespace chiplet
