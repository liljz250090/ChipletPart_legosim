#pragma once

#include "ChipletPartIR.h"

namespace chiplet {

class ChipletPartOpenDBReader {
public:
  IRDesign ReadDesign(void* db_block_handle) const;
};

}  // namespace chiplet
