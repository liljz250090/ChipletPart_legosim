#include "ChipletPartOpenDBReader.h"

#include <stdexcept>

#if defined(CHIPLETPART_ENABLE_OPENDB_BACKEND)
#include "odb/db.h"
#include <unordered_map>
#include <unordered_set>
#endif

namespace chiplet {

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

  auto* block = reinterpret_cast<odb::dbBlock*>(db_block_handle);
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
      ir_block.location_x
          = static_cast<float>(block->dbuToMicrons((rect.xMin() + rect.xMax()) / 2.0));
      ir_block.location_y
          = static_cast<float>(block->dbuToMicrons((rect.yMin() + rect.yMax()) / 2.0));
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
      if (inst == nullptr || master == nullptr || master->isPad() || master->isCover()) {
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
#endif
}

}  // namespace chiplet
