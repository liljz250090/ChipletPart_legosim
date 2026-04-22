#pragma once

#include "ChipletPartIR.h"

#include <filesystem>
#include <string>

namespace chiplet {

struct ChipletPartLegacyInputFiles {
  std::filesystem::path base_dir;
  std::string io_file;
  std::string layer_file;
  std::string wafer_process_file;
  std::string assembly_process_file;
  std::string test_file;
  std::string netlist_file;
  std::string blocks_file;
};

class ChipletPart3DBloxReader {
 public:
  IRDesign ReadDesign(const std::string& dbx_file,
                      const std::string& dbv_file = std::string()) const;

  ChipletPartLegacyInputFiles MaterializeLegacyInputs(
      const std::string& dbx_file,
      const std::filesystem::path& output_dir) const;
};

}  // namespace chiplet
