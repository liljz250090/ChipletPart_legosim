#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace chiplet {

struct OpenDBWriteStats {
  int partition_count = 0;
  int insts_written = 0;
  int bterms_written = 0;
  int groups_created = 0;
  int missing_vertices = 0;
  int missing_insts = 0;
  int missing_bterms = 0;
};

class ChipletPartOpenDBWriter {
public:
  OpenDBWriteStats WritePartition(
      void* db_block_handle,
      const std::vector<int>& solution,
      const std::unordered_map<int, std::string>& vertex_index_to_name) const;
};

}  // namespace chiplet
