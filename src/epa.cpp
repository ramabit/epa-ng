#include "epa.hpp"

#include <stdexcept>
#include <fstream>

#include "file_io.hpp"
#include "jplace_util.hpp"
#include "stringify.hpp"
#include "set_manipulators.hpp"
#include "logging.hpp"

using namespace std;

Log lgr;

void epa(string& tree_file, string& reference_msa_file, string& query_msa_file, string& outdir,
                Model model, Options options, string invocation)
{
	// sanitize input
  file_check(tree_file);
  file_check(reference_msa_file);
  if(query_msa_file.length() > 0)
    file_check(query_msa_file);
  if (outdir.length() > 0 && outdir.back() != '/')
    outdir += "/";
  lgr = Log(outdir + "epa_info.log");

  lgr << "EPA - Evolutionary Placement Algorithm" << endl;
  lgr << "\nInvocation: \n" << invocation << endl;

	// Build the reference tree
  auto ref_msa = build_MSA_from_file(reference_msa_file);

  MSA query_msa;
  if (query_msa_file.size() != 0)
    query_msa = build_MSA_from_file(query_msa_file);

  auto tree = Tree(tree_file, ref_msa, model, options, query_msa);

  // place query sequences
  auto pquerys = tree.place();

  ofstream outfile(outdir + "epa_result.jplace");
  outfile << pquery_set_to_jplace_string(pquerys, invocation) << endl;
  outfile.close();


	// for scalability...
		// stream? then probably conjuntion between pll datatype and my wrapper needed
		// partition? further heuristic!

	// subtask handling!

	// for basic approach: call kernel directly, basic for loop, later expand to par solution
		// kernel does one thing: place one sequence on one branch and recalculate
		// do for every branch
			// do for every sequence
	// loop returns list of list of pquerys

	// filter: what fraction of pquerys to remember (absolute number, top x %, threshold)

	// pass pquery lists to output writer, possibly also strategy based?
}
