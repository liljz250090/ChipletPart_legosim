///////////////////////////////////////////////////////////////////////////
//
// BSD 3-Clause License
//
// Copyright (c) 2022, The Regents of the University of California
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////////

#include "ChipletPart.h"
#include "ChipletPartOpenDBWriter.h"
#include "ChipletPart3DBloxReader.h"
#include "Hypergraph.h"
#include "OpenMPSupport.h"
#include "Utilities.h"
#include "evaluator_cpp.h" // Include the cost model evaluator_cpp.h instead of evaluator.h
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include <filesystem>
#include <numeric> // For std::accumulate

#if defined(CHIPLETPART_ENABLE_OPENDB_BACKEND)
#include "odb/db.h"
#endif

// Forward declarations for Console namespace
namespace Console {
  // ANSI color codes for terminal output
  const std::string RESET   = "\033[0m";
  const std::string BLACK   = "\033[30m";
  const std::string RED     = "\033[31m";
  const std::string GREEN   = "\033[32m";
  const std::string YELLOW  = "\033[33m";
  const std::string BLUE    = "\033[34m";
  const std::string MAGENTA = "\033[35m";
  const std::string CYAN    = "\033[36m";
  const std::string WHITE   = "\033[37m";
  
  // Text formatting
  const std::string BOLD    = "\033[1m";
  const std::string UNDERLINE = "\033[4m";
  
  void Info(const std::string& message);
  void Success(const std::string& message);
  void Warning(const std::string& message);
  void Error(const std::string& message);
  void Debug(const std::string& message);
  void Header(const std::string& message);
  void Subheader(const std::string& message);
}

// Function to print the application header
void printApplicationHeader() {
    std::cout << "\n\033[1;36m";  // Bold Cyan
    std::cout << "------------------------------------------------------------\n";
    std::cout << "            ChipletPart Partitioner / Evaluator             \n";
    std::cout << "                        Version: 1.0                        \n";
    std::cout << "------------------------------------------------------------\n";
    std::cout << "Developed by: UC San Diego and UC Los Angeles               \n";
    std::cout << "------------------------------------------------------------\n";
    std::cout << "\033[0m\n";  // Reset color
}

// Function to display program header
void displayHeader() {
  const std::string separator(60, '-');
  const std::string title("ChipletPart Partitioner / Evaluator");
  const std::string version("Version: 1.0");
  const std::string developedBy("Developed by: UC San Diego and UC Los Angeles");

  std::cout << std::endl;
  std::cout << separator << std::endl;
  std::cout << std::setw((separator.size() + title.length()) / 2) << title << std::endl;
  std::cout << std::setw((separator.size() + version.length()) / 2) << version << std::endl;
  std::cout << separator << std::endl;
  std::cout << developedBy << std::endl;
  std::cout << separator << std::endl;
  std::cout << std::endl;
}

// Function to display usage instructions
void displayUsage(const char* programName) {
  std::cout << "Usage: " << programName << " [options] <arguments>" << std::endl;
  std::cout << "Standard mode: " << programName << " <io_file> <layer_file> <wafer_process_file> <assembly_process_file> <test_file> <netlist_file> <blocks_file> <reach> <separation> <tech_node> [--seed <value>] [--threads <value>]" << std::endl;
  std::cout << "3dblox mode: " << programName << " --3dblox <design.3dbx> [--3dbv <library.3dbv>] <reach> <separation> <tech_node> [--seed <value>] [--threads <value>]" << std::endl;
  std::cout << "ODB mode: " << programName << " --odb <design.odb> <io_file> <layer_file> <wafer_process_file> <assembly_process_file> <test_file> <netlist_file> <blocks_file> <reach> <separation> <tech_node> [--seed <value>] [--threads <value>]" << std::endl;
  std::cout << "ODB readback mode: " << programName << " --odb <design.odb> --read-cpart <result.cpart.N> --map-file <output.map> [--odb-out <design.odb>]" << std::endl;
  std::cout << "Evaluation mode: " << programName << " <partition_file> <io_file> <layer_file> <wafer_process_file> <assembly_process_file> <test_file> <netlist_file> <blocks_file> <reach> <separation> <tech_node> [--seed <value>] [--threads <value>]" << std::endl;
  std::cout << "Canonical GA: " << programName << " <io_file> <layer_file> <wafer_process_file> <assembly_process_file> <test_file> <netlist_file> <blocks_file> <reach> <separation> --canonical-ga --tech-nodes <list> [--seed <value>] [--threads <value>] [--generations <value>] [--population <value>]" << std::endl;
  std::cout << "Tech Enumeration: " << programName << " <io_file> <layer_file> <wafer_process_file> <assembly_process_file> <test_file> <netlist_file> <blocks_file> <reach> <separation> --tech-enum --tech-nodes <list> [--max-partitions <value>] [--detailed-output] [--seed <value>] [--threads <value>]" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  --seed <value>        : Random seed for reproducible results (default: 42)" << std::endl;
  std::cout << "  --threads <value>     : Override OpenMP thread count for reproducible comparisons" << std::endl;
  std::cout << "  --3dblox <file>       : Read a self-contained 3dblox design (.3dbx) as the frontend input" << std::endl;
  std::cout << "  --3dbv <file>         : Optional companion 3dbv file for 3dblox mode" << std::endl;
  std::cout << "  --odb <file>          : Read an OpenDB database (.odb) as the frontend input" << std::endl;
  std::cout << "  --read-cpart <file>   : Read a ChipletPart .cpart result and write it back into the ODB" << std::endl;
  std::cout << "  --map-file <file>     : Read the accompanying output.map to resolve vertex names during ODB writeback" << std::endl;
  std::cout << "  --odb-out <file>      : Optional ODB output path for readback mode; defaults to overwriting --odb" << std::endl;
  std::cout << "  --canonical-ga        : Use canonical genetic algorithm for technology assignment" << std::endl;
  std::cout << "  --tech-enum           : Enumerate all canonical technology assignments up to max partitions" << std::endl;
  std::cout << "  --tech-nodes <list>   : Comma-separated list of technology nodes (e.g., '7nm,14nm,28nm')" << std::endl;
  std::cout << "  --generations <value> : Number of generations for genetic algorithm (default: 50)" << std::endl;
  std::cout << "  --population <value>  : Population size for genetic algorithm (default: 50)" << std::endl;
  std::cout << "  --max-partitions <value> : Maximum number of partitions for tech enumeration (default: 4)" << std::endl;
  std::cout << "  --detailed-output     : Generate detailed output for tech enumeration" << std::endl;
  std::cout << "Examples:" << std::endl;
  std::cout << "  " << programName << " io.xml layer.xml wafer.xml assembly.xml test.xml netlist.xml blocks.txt 0.5 0.25 7nm" << std::endl;
  std::cout << "  " << programName << " --3dblox ga100.3dbx --3dbv ga100.3dbv 0.5 0.25 7nm" << std::endl;
  std::cout << "  " << programName << " --odb ga100.odb io.xml layer.xml wafer.xml assembly.xml test.xml netlist.xml blocks.txt 0.5 0.25 7nm" << std::endl;
  std::cout << "  " << programName << " --odb ga100.odb --read-cpart block_level_netlist.xml.cpart.5 --map-file output.map" << std::endl;
  std::cout << "  " << programName << " io.xml layer.xml wafer.xml assembly.xml test.xml netlist.xml blocks.txt 0.5 0.25 --canonical-ga --tech-nodes 7nm,14nm,28nm --seed 123" << std::endl;
  std::cout << "  " << programName << " io.xml layer.xml wafer.xml assembly.xml test.xml netlist.xml blocks.txt 0.5 0.25 --tech-enum --tech-nodes 7nm,14nm,28nm --max-partitions 3" << std::endl;
}

// Function to parse a comma-separated list of technologies
std::vector<std::string> parseTechList(const std::string& techStr) {
  std::vector<std::string> techs;
  
  // Handle empty string
  if (techStr.empty()) {
    return techs;
  }
  
  try {
    // Make a local copy of the input string to ensure memory safety
    std::string safe_tech_str(techStr.c_str());
    
    size_t start = 0;
    size_t pos = safe_tech_str.find(',');
    
    // Handle single tech node without commas
    if (pos == std::string::npos) {
      // Create a new string from the tech node
      std::string tech(safe_tech_str.c_str());
      techs.push_back(tech);
      return techs;
    }
    
    // Process comma-separated list
    while (pos != std::string::npos) {
      if (pos > start) { // Ensure we don't add empty strings from sequential commas
        // Get substring and ensure it's a proper new string
        std::string tech(safe_tech_str.substr(start, pos - start).c_str());
        if (!tech.empty()) {
          techs.push_back(tech);
        }
      }
      start = pos + 1;
      
      // Check bounds to prevent out-of-range errors
      if (start >= safe_tech_str.length()) {
        break;
      }
      
      pos = safe_tech_str.find(',', start);
    }
    
    // Add the last tech (or the only one if there were no commas)
    if (start < safe_tech_str.length()) {
      // Create a new string for the last tech node
      std::string lastTech(safe_tech_str.substr(start).c_str());
      if (!lastTech.empty()) {
        techs.push_back(lastTech);
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "Error parsing tech list: " << e.what() << std::endl;
    // Return an empty vector on error
    return std::vector<std::string>();
  }
  
  return techs;
}

// Function to safely convert string to float with error checking
float safeStof(const std::string& str, const std::string& paramName) {
  try {
    return std::stof(str);
  } catch (const std::exception& e) {
    throw std::runtime_error("Error parsing " + paramName + " value '" + str + "': " + e.what());
  }
}

// Function to safely convert string to int with error checking
int safeStoi(const std::string& str, const std::string& paramName) {
  try {
    return std::stoi(str);
  } catch (const std::exception& e) {
    throw std::runtime_error("Error parsing " + paramName + " value '" + str + "': " + e.what());
  }
}

// Function to check if a string argument is present and get its value
bool getArgValue(int argc, char* argv[], const std::string& option, std::string& value) {
  for (int i = 1; i < argc - 1; ++i) {
    if (option == argv[i]) {
      value = argv[i + 1];
      return true;
    }
  }
  return false;
}

// Function to check if a flag is present
bool hasFlag(int argc, char* argv[], const std::string& option) {
  for (int i = 1; i < argc; ++i) {
    if (option == argv[i]) {
      return true;
    }
  }
  return false;
}

#if defined(CHIPLETPART_ENABLE_OPENDB_BACKEND)
odb::dbDatabase* loadOdbDatabase(const std::string& odb_file)
{
  auto* db = odb::dbDatabase::create();
  std::ifstream file(odb_file, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open ODB file: " + odb_file);
  }
  db->read(file);
  return db;
}

void writeOdbDatabase(odb::dbDatabase* db, const std::string& odb_file)
{
  std::ofstream file(odb_file, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open ODB output file: " + odb_file);
  }
  db->write(file);
}

std::vector<int> loadPartitionVector(const std::string& partition_file)
{
  std::ifstream file(partition_file);
  if (!file) {
    throw std::runtime_error("Failed to open partition file: " + partition_file);
  }

  std::vector<int> partition_ids;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    try {
      partition_ids.push_back(std::stoi(line));
    } catch (const std::exception& e) {
      throw std::runtime_error("Error parsing partition value '" + line
                               + "': " + e.what());
    }
  }
  return partition_ids;
}

std::unordered_map<int, std::string> loadVertexMap(const std::string& map_file)
{
  std::ifstream file(map_file);
  if (!file) {
    throw std::runtime_error("Failed to open map file: " + map_file);
  }

  std::unordered_map<int, std::string> index_to_name;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    std::istringstream stream(line);
    int index = 0;
    std::string name;
    if (!(stream >> index >> name)) {
      throw std::runtime_error("Malformed map line: " + line);
    }
    index_to_name[index - 1] = name;
  }
  return index_to_name;
}
#endif

// Add this function after other command handling functions
void run_tech_enum(
    int argc, 
    char** argv, 
    chiplet::ChipletPart& chiplet_part, 
    const std::string& chiplet_io_file,
    const std::string& chiplet_layer_file,
    const std::string& chiplet_wafer_process_file,
    const std::string& chiplet_assembly_process_file,
    const std::string& chiplet_test_file,
    const std::string& chiplet_netlist_file,
    const std::string& chiplet_blocks_file,
    float reach,
    float separation,
    int seed) {
    
    // Parse tech nodes from command-line arguments or use defaults
    std::vector<std::string> tech_nodes;
    std::string tech_nodes_str;
    
    try {
        if (getArgValue(argc, argv, "--tech-nodes", tech_nodes_str)) {
            // Create a local copy of the string to ensure memory safety
            std::string safe_tech_str(tech_nodes_str.c_str());
            
            // Use safe string parsing to create the tech nodes vector
            tech_nodes = parseTechList(safe_tech_str);
            
            // Verify we have valid tech nodes
            if (!tech_nodes.empty()) {
                std::stringstream node_list;
                for (size_t i = 0; i < tech_nodes.size(); ++i) {
                    if (i > 0) node_list << ", ";
                    node_list << tech_nodes[i];
                }
                Console::Info("Using specified tech nodes: " + node_list.str());
            } else {
                Console::Warning("Failed to parse tech nodes, falling back to defaults");
                // Default technology nodes as individual strings
                tech_nodes.push_back(std::string("7nm"));
                tech_nodes.push_back(std::string("14nm"));
                tech_nodes.push_back(std::string("10nm"));
            }
        } else {
            // Default technology nodes as individual strings
            tech_nodes.push_back(std::string("7nm"));
            tech_nodes.push_back(std::string("14nm"));
            tech_nodes.push_back(std::string("10nm"));
            Console::Info("Using default tech nodes: 7nm, 14nm, 10nm");
        }
        
        // Get max_partitions parameter
        int max_partitions = 4; // Default max partitions
        std::string max_parts_str;
        if (getArgValue(argc, argv, "--max-partitions", max_parts_str)) {
            max_partitions = safeStoi(max_parts_str, "max_partitions");
            Console::Info("Maximum partitions set to: " + std::to_string(max_partitions));
        } else {
            Console::Info("Using default maximum partitions: 4");
        }
        
        // Check if detailed output is requested
        bool detailed_output = hasFlag(argc, argv, "--detailed-output");
        if (detailed_output) {
            Console::Info("Detailed output enabled");
        }
        
        // Run the technology enumeration algorithm
        if (tech_nodes.empty()) {
            Console::Error("No valid technology nodes found.");
            throw std::runtime_error("No valid technology nodes provided");
        }
        
        // Make sure files exist before running
        std::ifstream io_file(chiplet_io_file);
        if (!io_file.good()) {
            Console::Error("IO definitions file not found: " + chiplet_io_file);
            throw std::runtime_error("IO definitions file not found: " + chiplet_io_file);
        }
        
        // Call the technology enumeration method
        auto [best_cost, best_partition, best_tech_assignment] = chiplet_part.EnumerateTechAssignments(
            chiplet_io_file,
            chiplet_layer_file,
            chiplet_wafer_process_file,
            chiplet_assembly_process_file,
            chiplet_test_file,
            chiplet_netlist_file,
            chiplet_blocks_file,
            reach,
            separation,
            tech_nodes,
            max_partitions,
            detailed_output
        );
        
        // Display summary of results
        Console::Header("Technology Enumeration Summary");
        Console::Success("Best cost: " + std::to_string(best_cost));
        Console::Success("Number of partitions in best solution: " + std::to_string(best_tech_assignment.size()));
        
        // Format the best technology assignment as a string
        std::string best_tech_str = "[";
        for (size_t i = 0; i < best_tech_assignment.size(); i++) {
            best_tech_str += best_tech_assignment[i];
            if (i < best_tech_assignment.size() - 1) best_tech_str += ", ";
        }
        best_tech_str += "]";
        
        Console::Success("Best technology assignment: " + best_tech_str);
    } catch (const std::exception& e) {
        Console::Error("Exception in technology enumeration: " + std::string(e.what()));
        throw; // Re-throw to ensure the caller knows there was an error
    }
}

// Add this function after other command handling functions

void run_canonical_ga(
    int argc, 
    char** argv, 
    chiplet::ChipletPart& chiplet_part, 
    const std::string& chiplet_io_file,
    const std::string& chiplet_layer_file,
    const std::string& chiplet_wafer_process_file,
    const std::string& chiplet_assembly_process_file,
    const std::string& chiplet_test_file,
    const std::string& chiplet_netlist_file,
    const std::string& chiplet_blocks_file,
    float reach,
    float separation,
    int seed,
    int population_size,
    int num_generations) {
    
    // Parse tech nodes from command-line arguments or use defaults
    std::vector<std::string> tech_nodes;
    std::string tech_nodes_str;
    
    try {
        if (getArgValue(argc, argv, "--tech-nodes", tech_nodes_str)) {
            // Create a local copy of the string to ensure memory safety
            std::string safe_tech_str(tech_nodes_str.c_str());
            
            // Use safe string parsing to create the tech nodes vector
            tech_nodes = parseTechList(safe_tech_str);
            
            // Verify we have valid tech nodes
            if (!tech_nodes.empty()) {
                std::stringstream node_list;
                for (size_t i = 0; i < tech_nodes.size(); ++i) {
                    if (i > 0) node_list << ", ";
                    node_list << tech_nodes[i];
                }
                Console::Info("Using specified tech nodes: " + node_list.str());
            } else {
                Console::Warning("Failed to parse tech nodes, falling back to defaults");
                // Default technology nodes as individual strings
                tech_nodes.push_back(std::string("7nm"));
                tech_nodes.push_back(std::string("14nm"));
                tech_nodes.push_back(std::string("10nm"));
            }
        } else {
            // Default technology nodes as individual strings
            tech_nodes.push_back(std::string("7nm"));
            tech_nodes.push_back(std::string("14nm"));
            tech_nodes.push_back(std::string("10nm"));
            Console::Info("Using default tech nodes: 7nm, 14nm, 10nm");
        }
        
        // Run the canonical genetic algorithm with check for empty tech_nodes
        if (tech_nodes.empty()) {
            Console::Error("No valid technology nodes found.");
            throw std::runtime_error("No valid technology nodes provided");
        }
        
        // Make sure files exist before running
        std::ifstream io_file(chiplet_io_file);
        if (!io_file.good()) {
            Console::Error("IO definitions file not found: " + chiplet_io_file);
            throw std::runtime_error("IO definitions file not found: " + chiplet_io_file);
        }
        
        // Call chiplet_part.CanonicalGeneticTechPart with the safe tech nodes vector
        chiplet_part.CanonicalGeneticTechPart(
            chiplet_io_file,
            chiplet_layer_file,
            chiplet_wafer_process_file,
            chiplet_assembly_process_file,
            chiplet_test_file,
            chiplet_netlist_file,
            chiplet_blocks_file,
            reach,
            separation,
            tech_nodes,
            population_size,
            num_generations,
            0.2,    // mutation_rate
            0.7,    // crossover_rate
            2,      // min_partitions
            8,      // max_partitions
            "canonical_ga_result"
        );
    } catch (const std::exception& e) {
        Console::Error("Exception in canonical GA: " + std::string(e.what()));
        throw; // Re-throw to ensure the caller knows there was an error
    }
}

int main(int argc, char *argv[]) {
  try {
    std::string dbxFile;
    const bool has3DBloxInput = getArgValue(argc, argv, "--3dblox", dbxFile);
    std::string odbFile;
    const bool hasODBInput = getArgValue(argc, argv, "--odb", odbFile);
    std::string dbvFile;
    const bool has3DBVInput = getArgValue(argc, argv, "--3dbv", dbvFile);
    std::string readCpartFile;
    const bool hasReadCpart = getArgValue(argc, argv, "--read-cpart", readCpartFile);
    std::string mapFile;
    const bool hasMapFile = getArgValue(argc, argv, "--map-file", mapFile);
    std::string odbOutFile;
    const bool hasODBOut = getArgValue(argc, argv, "--odb-out", odbOutFile);

    if (has3DBloxInput && hasODBInput) {
      throw std::runtime_error("Cannot use --3dblox and --odb together");
    }

    // Process seed parameter
    std::string seedStr;
    bool hasSeed = getArgValue(argc, argv, "--seed", seedStr);
    int seed = 42; // Default seed
    
    if (hasSeed) {
      seed = safeStoi(seedStr, "seed");
      Console::Info("Random seed set to " + std::to_string(seed));
    }

    std::string threadsStr;
    bool hasThreads = getArgValue(argc, argv, "--threads", threadsStr);
    int requestedThreads = -1;
    if (hasThreads) {
      requestedThreads = safeStoi(threadsStr, "threads");
      if (requestedThreads <= 0) {
        throw std::runtime_error("threads must be a positive integer");
      }
      omp_utils::set_num_threads(requestedThreads);
      Console::Info("OpenMP thread count forced to " + std::to_string(requestedThreads));
    }
    
    // Process genetic tech partitioning parameters
    bool useGeneticTechPart = false;
    for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--genetic-tech-part") {
        useGeneticTechPart = true;
        break;
      }
    }
    
    // Process canonical GA flag
    bool useCanonicalGA = false;
    for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--canonical-ga") {
        useCanonicalGA = true;
        break;
      }
    }
    
    // Process technology enumeration flag
    bool useTechEnum = false;
    for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--tech-enum") {
        useTechEnum = true;
        break;
      }
    }
    
    // Check for generations parameter
    std::string genStr;
    bool hasGenerations = getArgValue(argc, argv, "--generations", genStr);
    int generations = 50; // Default generations
    
    if (hasGenerations) {
      generations = safeStoi(genStr, "generations");
      Console::Info("Generations set to " + std::to_string(generations));
    }
    
    // Check for population parameter
    std::string popStr;
    bool hasPopulation = getArgValue(argc, argv, "--population", popStr);
    int population = 50; // Default population
    
    if (hasPopulation) {
      population = safeStoi(popStr, "population");
      Console::Info("Population size set to " + std::to_string(population));
    }
    
    // Print application header
    printApplicationHeader();
    
    // Create ChipletPart instance
    auto chiplet_part = std::make_shared<chiplet::ChipletPart>(seed);

    if (hasODBInput) {
#if !defined(CHIPLETPART_ENABLE_OPENDB_BACKEND)
      throw std::runtime_error(
          "This build was compiled without OpenDB backend support; rebuild with "
          "ENABLE_OPENDB_BACKEND=ON.");
#else
      std::vector<std::string> cleanArgs;
      for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--seed" || arg == "--threads" || arg == "--odb"
            || arg == "--odb-out" || arg == "--read-cpart"
            || arg == "--map-file") {
          ++i;
          continue;
        }
        if (arg == "--canonical-ga" || arg == "--tech-enum"
            || arg == "--genetic-tech-part" || arg == "--detailed-output") {
          continue;
        }
        if (arg == "--generations" || arg == "--population"
            || arg == "--max-partitions" || arg == "--tech-nodes") {
          ++i;
          continue;
        }
        cleanArgs.push_back(arg);
      }

      odb::dbDatabase* db = loadOdbDatabase(odbFile);
      odb::dbChip* chip = db != nullptr ? db->getChip() : nullptr;
      odb::dbBlock* block = chip != nullptr ? chip->getBlock() : nullptr;
      if (chip == nullptr) {
        throw std::runtime_error("ODB input does not contain a top dbChip: " + odbFile);
      }

      if (hasSeed) {
        chiplet_part->SetSeed(seed);
      }
      chiplet_part->SetOpenDBInput(block != nullptr ? static_cast<void*>(block)
                                                   : static_cast<void*>(chip));

      Console::Info("Partitioning using ODB frontend input");
      Console::Info("Loaded ODB database from " + odbFile);
      if (block == nullptr) {
        Console::Info("Top-level ODB object is a hierarchy dbChip; using embedded property metadata");
      }

      if (hasReadCpart || hasMapFile) {
        if (!hasReadCpart || !hasMapFile) {
          throw std::runtime_error(
              "ODB readback mode requires both --read-cpart and --map-file");
        }
        auto solution = loadPartitionVector(readCpartFile);
        auto vertex_map = loadVertexMap(mapFile);
        if (solution.size() != vertex_map.size()) {
          throw std::runtime_error(
              "Mismatch between cpart entries (" + std::to_string(solution.size())
              + ") and map entries (" + std::to_string(vertex_map.size()) + ")");
        }

        chiplet::ChipletPartOpenDBWriter writer;
        const auto stats = writer.WritePartition(
            block != nullptr ? static_cast<void*>(block) : static_cast<void*>(chip),
            solution,
            vertex_map);
        const std::filesystem::path outputPath
            = hasODBOut ? std::filesystem::path(odbOutFile)
                        : std::filesystem::path(odbFile);
        writeOdbDatabase(db, outputPath.string());
        Console::Info("OpenDB readback complete: insts="
                      + std::to_string(stats.insts_written)
                      + ", bterms=" + std::to_string(stats.bterms_written)
                      + ", groups=" + std::to_string(stats.groups_created)
                      + ", partition_count="
                      + std::to_string(stats.partition_count));
        Console::Info("Wrote ODB writeback result to " + outputPath.string());
        return 0;
      }

      if (useTechEnum) {
        if (cleanArgs.size() != 9) {
          Console::Error("ODB tech-enum mode expects <io> <layer> <wafer> <assembly> <test> <netlist> <blocks> <reach> <separation>");
          displayUsage(argv[0]);
          return 1;
        }
        run_tech_enum(argc,
                      argv,
                      *chiplet_part,
                      cleanArgs[0],
                      cleanArgs[1],
                      cleanArgs[2],
                      cleanArgs[3],
                      cleanArgs[4],
                      cleanArgs[5],
                      cleanArgs[6],
                      safeStof(cleanArgs[7], "reach"),
                      safeStof(cleanArgs[8], "separation"),
                      seed);
        return 0;
      }

      if (useCanonicalGA) {
        if (cleanArgs.size() != 9) {
          Console::Error("ODB canonical-ga mode expects <io> <layer> <wafer> <assembly> <test> <netlist> <blocks> <reach> <separation>");
          displayUsage(argv[0]);
          return 1;
        }
        run_canonical_ga(argc,
                         argv,
                         *chiplet_part,
                         cleanArgs[0],
                         cleanArgs[1],
                         cleanArgs[2],
                         cleanArgs[3],
                         cleanArgs[4],
                         cleanArgs[5],
                         cleanArgs[6],
                         safeStof(cleanArgs[7], "reach"),
                         safeStof(cleanArgs[8], "separation"),
                         seed,
                         population,
                         generations);
        return 0;
      }

      if (useGeneticTechPart) {
        if (cleanArgs.size() != 9) {
          Console::Error("ODB genetic-tech-part mode expects <io> <layer> <wafer> <assembly> <test> <netlist> <blocks> <reach> <separation>");
          displayUsage(argv[0]);
          return 1;
        }

        std::vector<std::string> techNodes;
        std::string techNodesStr;
        if (getArgValue(argc, argv, "--tech-nodes", techNodesStr)) {
          techNodes = parseTechList(techNodesStr);
        }
        if (techNodes.empty()) {
          Console::Error("No tech nodes specified for genetic tech partitioning");
          return 1;
        }

        chiplet_part->GeneticTechPart(cleanArgs[0],
                                      cleanArgs[1],
                                      cleanArgs[2],
                                      cleanArgs[3],
                                      cleanArgs[4],
                                      cleanArgs[5],
                                      cleanArgs[6],
                                      safeStof(cleanArgs[7], "reach"),
                                      safeStof(cleanArgs[8], "separation"),
                                      techNodes,
                                      population,
                                      generations);
        return 0;
      }

      if (cleanArgs.size() != 10) {
        Console::Error("ODB mode expects <io> <layer> <wafer> <assembly> <test> <netlist> <blocks> <reach> <separation> <tech_node>");
        displayUsage(argv[0]);
        return 1;
      }

      const float reach = safeStof(cleanArgs[7], "reach");
      const float separation = safeStof(cleanArgs[8], "separation");
      const std::string tech = cleanArgs[9];

      if (tech.find(',') != std::string::npos) {
        chiplet_part->TechAssignPartition(cleanArgs[0],
                                          cleanArgs[1],
                                          cleanArgs[2],
                                          cleanArgs[3],
                                          cleanArgs[4],
                                          cleanArgs[5],
                                          cleanArgs[6],
                                          reach,
                                          separation,
                                          parseTechList(tech));
      } else {
        chiplet_part->Partition(cleanArgs[0],
                                cleanArgs[1],
                                cleanArgs[2],
                                cleanArgs[3],
                                cleanArgs[4],
                                cleanArgs[5],
                                cleanArgs[6],
                                reach,
                                separation,
                                tech);
      }
      return 0;
#endif
    }

    if (has3DBloxInput) {
      std::vector<std::string> cleanArgs;
      for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--seed" || arg == "--threads" || arg == "--3dblox" || arg == "--3dbv"
            || arg == "--canonical-ga" || arg == "--tech-enum"
            || arg == "--genetic-tech-part" || arg == "--detailed-output") {
          ++i;
          if (arg == "--canonical-ga" || arg == "--tech-enum"
              || arg == "--genetic-tech-part" || arg == "--detailed-output") {
            --i;
          }
          continue;
        }
        if (arg == "--generations" || arg == "--population"
            || arg == "--max-partitions") {
          ++i;
          continue;
        }
        if (arg == "--tech-nodes") {
          while (i + 1 < argc) {
            const std::string next_arg = argv[i + 1];
            if (next_arg.rfind("--", 0) == 0) {
              break;
            }
            ++i;
          }
          continue;
        }
        cleanArgs.push_back(arg);
      }

      chiplet::ChipletPart3DBloxReader reader;
      const auto temp_root = std::filesystem::temp_directory_path()
                             / ("chipletpart_3dblox_" + std::to_string(
                                                    static_cast<long long>(
                                                        std::chrono::high_resolution_clock::now()
                                                            .time_since_epoch()
                                                            .count())));
      const auto legacy_files = reader.MaterializeLegacyInputs(dbxFile, temp_root);

      if (hasSeed) {
        chiplet_part->SetSeed(seed);
      }
      chiplet_part->Set3DBloxInput(dbxFile, dbvFile);

      Console::Info("Partitioning using 3dblox frontend input");
      Console::Info("Materialized temporary collateral in " + legacy_files.base_dir.string());

      if (useTechEnum) {
        if (cleanArgs.size() != 2) {
          Console::Error("3dblox tech-enum mode expects <reach> <separation>");
          displayUsage(argv[0]);
          return 1;
        }
        float reach = safeStof(cleanArgs[0], "reach");
        float separation = safeStof(cleanArgs[1], "separation");
        run_tech_enum(argc,
                      argv,
                      *chiplet_part,
                      legacy_files.io_file,
                      legacy_files.layer_file,
                      legacy_files.wafer_process_file,
                      legacy_files.assembly_process_file,
                      legacy_files.test_file,
                      legacy_files.netlist_file,
                      legacy_files.blocks_file,
                      reach,
                      separation,
                      seed);
        return 0;
      }

      if (useCanonicalGA) {
        if (cleanArgs.size() != 2) {
          Console::Error("3dblox canonical-ga mode expects <reach> <separation>");
          displayUsage(argv[0]);
          return 1;
        }
        float reach = safeStof(cleanArgs[0], "reach");
        float separation = safeStof(cleanArgs[1], "separation");
        run_canonical_ga(argc,
                         argv,
                         *chiplet_part,
                         legacy_files.io_file,
                         legacy_files.layer_file,
                         legacy_files.wafer_process_file,
                         legacy_files.assembly_process_file,
                         legacy_files.test_file,
                         legacy_files.netlist_file,
                         legacy_files.blocks_file,
                         reach,
                         separation,
                         seed,
                         population,
                         generations);
        return 0;
      }

      if (useGeneticTechPart) {
        if (cleanArgs.size() != 2) {
          Console::Error("3dblox genetic-tech-part mode expects <reach> <separation>");
          displayUsage(argv[0]);
          return 1;
        }

        std::vector<std::string> techNodes;
        std::string techNodesStr;
        if (getArgValue(argc, argv, "--tech-nodes", techNodesStr)) {
          techNodes = parseTechList(techNodesStr);
        }
        if (techNodes.empty()) {
          Console::Error("No tech nodes specified for genetic tech partitioning");
          return 1;
        }

        float reach = safeStof(cleanArgs[0], "reach");
        float separation = safeStof(cleanArgs[1], "separation");
        chiplet_part->GeneticTechPart(legacy_files.io_file,
                                      legacy_files.layer_file,
                                      legacy_files.wafer_process_file,
                                      legacy_files.assembly_process_file,
                                      legacy_files.test_file,
                                      legacy_files.netlist_file,
                                      legacy_files.blocks_file,
                                      reach,
                                      separation,
                                      techNodes,
                                      population,
                                      generations);
        return 0;
      }

      if (cleanArgs.size() != 3) {
        Console::Error("3dblox mode expects <reach> <separation> <tech_node>");
        displayUsage(argv[0]);
        return 1;
      }

      const float reach = safeStof(cleanArgs[0], "reach");
      const float separation = safeStof(cleanArgs[1], "separation");
      const std::string tech = cleanArgs[2];

      if (tech.find(',') != std::string::npos) {
        std::vector<std::string> techs = parseTechList(tech);
        chiplet_part->TechAssignPartition(legacy_files.io_file,
                                          legacy_files.layer_file,
                                          legacy_files.wafer_process_file,
                                          legacy_files.assembly_process_file,
                                          legacy_files.test_file,
                                          legacy_files.netlist_file,
                                          legacy_files.blocks_file,
                                          reach,
                                          separation,
                                          techs);
      } else {
        chiplet_part->Partition(legacy_files.io_file,
                                legacy_files.layer_file,
                                legacy_files.wafer_process_file,
                                legacy_files.assembly_process_file,
                                legacy_files.test_file,
                                legacy_files.netlist_file,
                                legacy_files.blocks_file,
                                reach,
                                separation,
                                tech);
      }
      return 0;
    }
    
    // Technology enumeration mode
    if (useTechEnum) {
      try {
        // Create a clean list of arguments without optional flags and their values
        std::vector<std::string> cleanArgs;
        for (int i = 1; i < argc; i++) {
          std::string arg = argv[i];
          if (arg == "--tech-enum" || arg == "--detailed-output") {
            continue; // Skip the flag itself
          }
          if (arg == "--seed" || arg == "--threads" || arg == "--max-partitions" || arg == "--tech-nodes") {
            i++; // Skip the flag and its value
            continue;
          }
          cleanArgs.push_back(arg);
        }
        
        // Check if we have enough arguments for technology enumeration
        if (cleanArgs.size() < 9) {
          Console::Error("Not enough required arguments for technology enumeration");
          displayUsage(argv[0]);
          return 1;
        }
        
        // Now process the required positional arguments
        std::string io_definitions_file = cleanArgs[0];
        std::string layer_definitions_file = cleanArgs[1];
        std::string wafer_process_definitions_file = cleanArgs[2];
        std::string assembly_process_definitions_file = cleanArgs[3];
        std::string test_definitions_file = cleanArgs[4];
        std::string block_level_netlist_file = cleanArgs[5];
        std::string block_definitions_file = cleanArgs[6];
        float reach = safeStof(cleanArgs[7], "reach");
        float separation = safeStof(cleanArgs[8], "separation");
        
        // Run the technology enumeration algorithm
        run_tech_enum(
            argc,
            argv,
            *chiplet_part,
            io_definitions_file,
            layer_definitions_file,
            wafer_process_definitions_file,
            assembly_process_definitions_file,
            test_definitions_file,
            block_level_netlist_file,
            block_definitions_file,
            reach,
            separation,
            seed
        );
        
        return 0;
      } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
      } catch (...) {
        std::cerr << "Error: Unknown exception occurred" << std::endl;
        return 1;
      }
    }
    
    // For genetic tech partitioning, we need a different approach to argument parsing
    if (useGeneticTechPart) {
      // Create a list of all arguments to parse
      std::vector<std::string> args;
      for (int i = 1; i < argc; i++) {
        args.push_back(argv[i]);
      }
      
      // Find the indices of required fixed arguments
      int firstOptionalIdx = -1;
      for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--genetic-tech-part" || args[i] == "--seed" || args[i] == "--threads" ||
            args[i] == "--generations" || args[i] == "--population" ||
            args[i] == "--tech-nodes") {
          if (firstOptionalIdx == -1 || static_cast<int>(i) < firstOptionalIdx) {
            firstOptionalIdx = i;
          }
        }
      }

      if (firstOptionalIdx == -1) {
        firstOptionalIdx = static_cast<int>(args.size());
      }
      
      if (firstOptionalIdx < 9) {
        Console::Error("Not enough required arguments for genetic tech partitioning");
        displayUsage(argv[0]);
        return 1;
      }
      
      // Parse the standard arguments (first 10 args before any optional ones)
      std::string io_definitions_file = args[0];
      std::string layer_definitions_file = args[1];
      std::string wafer_process_definitions_file = args[2];
      std::string assembly_process_definitions_file = args[3];
      std::string test_definitions_file = args[4];
      std::string block_level_netlist_file = args[5];
      std::string block_definitions_file = args[6];
      float reach = safeStof(args[7], "reach");
      float separation = safeStof(args[8], "separation");
      
      // Parse tech nodes from --tech-nodes option
      std::vector<std::string> techNodes;
      std::string techNodesStr;
      if (getArgValue(argc, argv, "--tech-nodes", techNodesStr)) {
        techNodes = parseTechList(techNodesStr);
      }
      
      if (techNodes.empty()) {
        Console::Error("No tech nodes specified for genetic tech partitioning");
        displayUsage(argv[0]);
        return 1;
      }
      
      Console::Info("Running genetic tech partitioning");
      Console::Info("Tech nodes: " + std::accumulate(techNodes.begin(), techNodes.end(), std::string(),
                                             [](const std::string& a, const std::string& b) {
                                               return a + (a.empty() ? "" : ", ") + b;
                                             }));
      
      // Set seed if provided
      if (hasSeed) {
        chiplet_part->SetSeed(seed);
      }
      
      // Run the genetic tech partitioning
      chiplet_part->GeneticTechPart(
          io_definitions_file, layer_definitions_file, wafer_process_definitions_file,
          assembly_process_definitions_file, test_definitions_file, block_level_netlist_file, 
          block_definitions_file, reach, separation, techNodes,
          population, generations);
      
      return 0;
    }
    // Special handling for canonical GA mode
    else if (useCanonicalGA) {
      try {
        // Create a clean list of arguments without optional flags and their values
        std::vector<std::string> cleanArgs;
        for (int i = 1; i < argc; i++) {
          std::string arg = argv[i];
          if (arg == "--canonical-ga") {
            continue; // Skip the flag itself
          }
          if (arg == "--seed" || arg == "--threads" || arg == "--generations" || arg == "--population" || arg == "--tech-nodes") {
            i++; // Skip the flag and its value
            continue;
          }
          cleanArgs.push_back(arg);
        }
        
        // Check if we have enough arguments for canonical GA
        if (cleanArgs.size() < 9) {
          Console::Error("Not enough required arguments for canonical GA");
          displayUsage(argv[0]);
          return 1;
        }
        
        // Now process the required positional arguments
        std::string io_definitions_file = cleanArgs[0];
        std::string layer_definitions_file = cleanArgs[1];
        std::string wafer_process_definitions_file = cleanArgs[2];
        std::string assembly_process_definitions_file = cleanArgs[3];
        std::string test_definitions_file = cleanArgs[4];
        std::string block_level_netlist_file = cleanArgs[5];
        std::string block_definitions_file = cleanArgs[6];
        float reach = safeStof(cleanArgs[7], "reach");
        float separation = safeStof(cleanArgs[8], "separation");
        
        // Get tech nodes
        std::vector<std::string> techNodes;
        std::string techNodesStr;
        if (getArgValue(argc, argv, "--tech-nodes", techNodesStr)) {
          techNodes = parseTechList(techNodesStr);
          Console::Info("Using specified tech nodes: " + techNodesStr);
        } else {
          // Default technology nodes
          techNodes = {"7nm", "14nm", "28nm"};
          Console::Info("Using default tech nodes: 7nm, 14nm, 28nm");
        }
        
        // Run the canonical GA algorithm
        run_canonical_ga(
            argc,
            argv,
            *chiplet_part,
            io_definitions_file,
            layer_definitions_file,
            wafer_process_definitions_file,
            assembly_process_definitions_file,
            test_definitions_file,
            block_level_netlist_file,
            block_definitions_file,
            reach,
            separation,
            seed,
            population,
            generations
        );
        
        return 0;
      } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
      } catch (...) {
        std::cerr << "Error: Unknown exception occurred" << std::endl;
        return 1;
      }
    }
    
    // The rest of the code for standard partitioning modes
    int effectiveArgc = argc;
    
    // Adjust effectiveArgc if --seed is used
    if (hasSeed) {
      effectiveArgc -= 2; // Remove --seed and its value from the count
    }
    if (hasThreads) {
      effectiveArgc -= 2; // Remove --threads and its value from the count
    }
    
    // Check if we have the correct number of arguments
    if (effectiveArgc < 11 || effectiveArgc > 12) {
      displayUsage(argv[0]);
      return 1;
    }
    
    // Set seed if provided
    if (hasSeed) {
      chiplet_part->SetSeed(seed);
    }
    
    // Determine the actual indices for the arguments
    int argOffset = 0;
    for (int i = 1; i < argc; i++) {
      if (std::string(argv[i]) == "--seed" || std::string(argv[i]) == "--threads") {
        i++; // Skip the seed value
        argOffset += 2;
      }
    }
    
    if (effectiveArgc == 11) {
      // Partitioning mode with XML input
      std::string io_definitions_file = argv[1 + argOffset * (argv[1] == std::string("--seed"))];
      std::string layer_definitions_file = argv[2 + argOffset * (argv[2] == std::string("--seed") || argv[1] == std::string("--seed"))];
      std::string wafer_process_definitions_file = argv[3 + argOffset * (argv[3] == std::string("--seed") || argv[2] == std::string("--seed") || argv[1] == std::string("--seed"))];
      std::string assembly_process_definitions_file = argv[4 + argOffset * (argv[4] == std::string("--seed") || argv[3] == std::string("--seed") || argv[2] == std::string("--seed") || argv[1] == std::string("--seed"))];
      std::string test_definitions_file = argv[5 + argOffset * (argv[5] == std::string("--seed") || argv[4] == std::string("--seed") || argv[3] == std::string("--seed") || argv[2] == std::string("--seed") || argv[1] == std::string("--seed"))];
      std::string block_level_netlist_file = argv[6 + argOffset * (argv[6] == std::string("--seed") || argv[5] == std::string("--seed") || argv[4] == std::string("--seed") || argv[3] == std::string("--seed") || argv[2] == std::string("--seed") || argv[1] == std::string("--seed"))];
      std::string block_definitions_file = argv[7 + argOffset * (argv[7] == std::string("--seed") || argv[6] == std::string("--seed") || argv[5] == std::string("--seed") || argv[4] == std::string("--seed") || argv[3] == std::string("--seed") || argv[2] == std::string("--seed") || argv[1] == std::string("--seed"))];
      
      // The logic above is complex and error-prone, let's simplify it:
      // Since the seed parameter can appear anywhere, we need a cleaner approach
      
      // Create a new array without the seed parameters
      std::vector<std::string> cleanArgs;
      for (int i = 0; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--seed" || arg == "--threads") {
          i++; // Skip the seed value
          continue;
        }
        if (i > 0) { // Skip program name
          cleanArgs.push_back(arg);
        }
      }
      
      // Now use the cleaned args
      io_definitions_file = cleanArgs[0];
      layer_definitions_file = cleanArgs[1];
      wafer_process_definitions_file = cleanArgs[2];
      assembly_process_definitions_file = cleanArgs[3];
      test_definitions_file = cleanArgs[4];
      block_level_netlist_file = cleanArgs[5];
      block_definitions_file = cleanArgs[6];
      
      float reach = safeStof(cleanArgs[7], "reach");
      float separation = safeStof(cleanArgs[8], "separation");
      std::string tech = cleanArgs[9];
      
      if (tech.find(',') != std::string::npos) {
        // Technology assignment mode (multiple technologies)
        std::vector<std::string> techs = parseTechList(tech);
        
        chiplet_part->TechAssignPartition(
            io_definitions_file, layer_definitions_file, wafer_process_definitions_file,
            assembly_process_definitions_file, test_definitions_file, block_level_netlist_file, 
            block_definitions_file, reach, separation, techs);
      } else {
        // Single technology partitioning
        std::cout << "[INFO] Partitioning using XML input files" << std::endl;
        
        chiplet_part->Partition(
            io_definitions_file, layer_definitions_file, wafer_process_definitions_file,
            assembly_process_definitions_file, test_definitions_file, block_level_netlist_file, 
            block_definitions_file, reach, separation, tech);
      }
    } else if (effectiveArgc == 12) {
      // Evaluation mode
      // Use the same cleanArgs approach for evaluation mode
      std::vector<std::string> cleanArgs;
      for (int i = 0; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--seed" || arg == "--threads") {
          i++; // Skip the seed value
          continue;
        }
        if (i > 0) { // Skip program name
          cleanArgs.push_back(arg);
        }
      }
      
      std::string hypergraph_part = cleanArgs[0];
      std::string io_definitions_file = cleanArgs[1];
      std::string layer_definitions_file = cleanArgs[2];
      std::string wafer_process_definitions_file = cleanArgs[3];
      std::string assembly_process_definitions_file = cleanArgs[4];
      std::string test_definitions_file = cleanArgs[5];
      std::string block_level_netlist_file = cleanArgs[6];
      std::string block_definitions_file = cleanArgs[7];
      
      float reach = safeStof(cleanArgs[8], "reach");
      float separation = safeStof(cleanArgs[9], "separation");
      std::string tech = cleanArgs[10];
      
      std::cout << "[INFO] Evaluating partition" << std::endl;
      
      chiplet_part->EvaluatePartition(
          hypergraph_part, io_definitions_file, layer_definitions_file, wafer_process_definitions_file,
          assembly_process_definitions_file, test_definitions_file, block_level_netlist_file, 
          block_definitions_file, reach, separation, tech);
    }
    
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}
