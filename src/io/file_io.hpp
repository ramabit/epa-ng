#pragma once

#include <string>
#include <tuple>
#include <limits>

#include "tree/Tree_Numbers.hpp"
#include "core/raxml/Model.hpp"
#include "seq/MSA.hpp"
#include "core/pll/pllhead.hpp"
#include "tree/Tree.hpp"

// forward declarations
class MSA;

MSA build_MSA_from_file(const std::string& msa_file,
                        const size_t offset=0,
                        const size_t span=std::numeric_limits<size_t>::max());
pll_utree_s * build_tree_from_file(const std::string& tree_file, Tree_Numbers& nums);
pll_partition_t * build_partition_from_file(const raxml::Model& model,
                                            Tree_Numbers& nums, 
                                            const int num_sites,
                                            const bool repeats = false);
void file_check(const std::string& file_path);
std::vector<size_t> get_offsets(const std::string& file, MSA& msa);
int pll_fasta_fseek(pll_fasta_t* fd, const long int offset, const int whence);
