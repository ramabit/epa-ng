#include "core/place.hpp"

#include <fstream>
#include <string>
#include <memory>
#include <functional>
#include <limits>

#ifdef __OMP
#include <omp.h>
#endif

#include "io/file_io.hpp"
#include "io/jplace_util.hpp"
#include "util/stringify.hpp"
#include "set_manipulators.hpp"
#include "util/logging.hpp"
#include "tree/Tiny_Tree.hpp"
#include "net/mpihead.hpp"
#include "core/pll/pll_util.hpp"
#include "core/pll/epa_pll_util.hpp"
#include "util/Timer.hpp"
#include "core/Work.hpp"
#include "pipeline/schedule.hpp"
#include "core/Lookup_Store.hpp"
#include "pipeline/Pipeline.hpp"
#include "seq/MSA.hpp"
#include "core/Work.hpp"
#include "sample/Sample.hpp"
#include "io/Binary_Fasta.hpp"

#ifdef __MPI
#include "net/epa_mpi_util.hpp"
#endif

template <class T>
static void place(const Work& to_place,
                  MSA& msa,
                  Tree& reference_tree,
                  const std::vector<pll_unode_t *>& branches,
                  Sample<T>& sample,
                  bool do_blo,
                  const Options& options,
                  std::shared_ptr<Lookup_Store>& lookup_store,
                  const size_t seq_id_offset=0)
{

#ifdef __OMP
  const unsigned int num_threads  = options.num_threads
                                  ? options.num_threads
                                  : omp_get_max_threads();
  omp_set_num_threads(num_threads);
  LOG_DBG << "Using threads: " << num_threads;
  LOG_DBG << "Max threads: " << omp_get_max_threads();
  const unsigned int multiplicity = 8;
#else
  const unsigned int num_threads = 1;
  const unsigned int multiplicity = 1;
#endif

  // split the sample structure such that the parts are thread-local
  std::vector<Sample<T>> sample_parts(num_threads);

  std::vector<Work> work_parts;
  split(to_place, work_parts, num_threads
                              * multiplicity);

  // work seperately
#ifdef __OMP
  #pragma omp parallel for schedule(dynamic)
#endif
  for (size_t i = 0; i < work_parts.size(); ++i) {
    auto prev_branch_id = std::numeric_limits<size_t>::max();

#ifdef __OMP
    const auto tid = omp_get_thread_num();
#else
    const auto tid = 0;
#endif
    std::shared_ptr<Tiny_Tree> branch(nullptr);

    for (const auto& it : work_parts[i]) {
      const auto branch_id = it.branch_id;
      const auto seq_id = it.sequence_id;
      const auto seq = msa[seq_id];

      if ((branch_id != prev_branch_id) or not branch) {
        branch = std::make_shared<Tiny_Tree>(branches[branch_id],
                                             branch_id,
                                             reference_tree,
                                             do_blo,
                                             options,
                                             lookup_store);
      }

      sample_parts[tid].add_placement(seq_id_offset + seq_id,
                                      seq.header(),
                                      branch->place(seq));

      prev_branch_id = branch_id;
    }
  }
  // merge samples back
  merge(sample, std::move(sample_parts));
  collapse(sample);
}

void pipeline_place(Tree& reference_tree,
                    const std::string& query_file,
                    const std::string& outdir,
                    const Options& options,
                    const std::string& invocation)
{
  LOG_INFO << "WARNING! THIS FUNCTION IS EXPERIMENTAL!" << std::endl;

  // Timer<> flight_time;
  std::ofstream flight_file(outdir + "stat");

  std::string status_file_name(outdir + "pepa.status");
  std::ofstream trunc_status_file(status_file_name, std::ofstream::trunc);

  std::vector<std::string> part_names;

  const auto chunk_size = options.chunk_size;
  LOG_DBG << "Chunk size: " << chunk_size << std::endl;

  const auto num_branches = reference_tree.nums().branches;

  // get all edges
  std::vector<pll_unode_t *> branches(num_branches);
  auto num_traversed_branches = utree_query_branches(reference_tree.tree(), &branches[0]);
  if (num_traversed_branches != num_branches) {
    throw std::runtime_error{"Traversing the utree went wrong during pipeline startup!"};
  }

  unsigned int chunk_num = 0;
  
  auto lookups = 
    std::make_shared<Lookup_Store>(num_branches, reference_tree.partition()->states);

  Work all_work(std::make_pair(0, num_branches), std::make_pair(0, chunk_size));
  
  MSA chunk;
  Binary_Fasta_Reader reader(query_file);

  size_t num_sequences = 0;

  // create output file
  std::ofstream outfile;

  using Slim_Sample = Sample<Slim_Placement>;
  using Sample      = Sample<Placement>;
  
  // ============ LAMBDAS ============================
  
  // only on one rank, only once at the beginning of pipeline
  auto init_pipe_func = [&]() -> void {
    outfile.open(outdir + "epa_result.jplace");
    auto newick_string = get_numbered_newick_string(reference_tree.tree());
    outfile << init_jplace_string(newick_string);
  };

  auto perloop_prehook = [&]() -> void {
    LOG_DBG << "INGESTING - READING" << std::endl;
    num_sequences = reader.read_next(chunk, chunk_size);
    ++chunk_num;
  };

  auto ingestion = [&](VoidToken&) -> Work {
    LOG_DBG << "INGESTING - CREATING WORK" << std::endl;
    if (num_sequences <= 0) {
      Work work;
      work.is_last(true);
      return work;
    } else if (num_sequences < chunk_size) {
      return Work(std::make_pair(0, num_branches), std::make_pair(0, num_sequences));
    } else {
      return all_work;
    }
  };

  auto preplacement = [&](Work& work) -> Slim_Sample {
    LOG_DBG << "PREPLACING" << std::endl;

    Slim_Sample result;

    place(work,
          chunk,
          reference_tree,
          branches,
          result,
          false,
          options,
          lookups);

    return result;
  };

  // auto ingest_preplace = [&](VoidToken&) -> Slim_Sample {
  //   LOG_DBG << "INGEST & PREPLACE" << std::endl;

  //   Slim_Sample result;

  //   if (num_sequences <= 0) {
  //     result.is_last(true);
  //     return result;
  //   } else if (num_sequences < chunk_size) {
  //     Work work(std::make_pair(0, num_branches), std::make_pair(0, num_sequences));
  //     std::vector<Work> parts
  //   } else {
  //     return all_work;
  //   }


  //   place(work,
  //         chunk,
  //         reference_tree,
  //         branches,
  //         result,
  //         false,
  //         options,
  //         lookups);

  //   return result;
  // };

  auto candidate_selection = [&](Slim_Sample& slim) -> Work {
    LOG_DBG << "SELECTING CANDIDATES" << std::endl;

    Sample sample(slim);

    compute_and_set_lwr(sample);

    if (options.prescoring_by_percentage) {
      discard_bottom_x_percent(sample, 
                              (1.0 - options.prescoring_threshold));
    } else {
      discard_by_accumulated_threshold(sample, 
                                      options.prescoring_threshold,
                                      options.filter_min,
                                      options.filter_max);
    }

    return Work(sample);
  };


  auto thorough_placement = [&](Work& work) -> Sample {
    LOG_DBG << "BLO PLACEMENT" << std::endl;

    Sample result;
    place(work,
          chunk,
          reference_tree,
          branches,
          result,
          true,
          options,
          lookups
    );
    return result;
  };

  auto write_result = [&](Sample& sample) -> VoidToken {
    LOG_DBG << "WRITING" << std::endl;

    compute_and_set_lwr(sample);
    if (options.acc_threshold) {
      LOG_DBG << "Filtering by accumulated threshold: " << options.support_threshold << std::endl;
      discard_by_accumulated_threshold( sample, 
                                        options.support_threshold,
                                        options.filter_min,
                                        options.filter_max);
    } else {
      LOG_DBG << "Filtering placements below threshold: " << options.support_threshold << std::endl;
      discard_by_support_threshold( sample,
                                    options.support_threshold,
                                    options.filter_min,
                                    options.filter_max);
    }


    // write results of current last stage aggregator node to a part file
    if (sample.size()) {
      if (chunk_num > 1) {
        outfile << ",";
      }
      outfile << sample_to_jplace_string(sample);
      // std::string part_file_name(outdir + "epa." + std::to_string(local_rank)
      //   + "." + std::to_string(chunk_num) + ".part");
      // std::ofstream part_file(part_file_name);
      // part_file << sample_to_jplace_string(sample, chunk);
      // part_names.push_back(part_file_name);
      // part_file.close();

      // // TODO for MPI, somehow ensure this code is only run on the stage foreman

      // std::ofstream status_file(status_file_name, std::ofstream::app);
      // status_file << chunk_num << ":" << chunk_size << " [";
      // for (size_t i = 0; i < part_names.size(); ++i) {
      //   status_file << part_names[i];
      //   if (i < part_names.size() - 1) status_file << ",";  
      // }
      // status_file << "]" << std::endl;
      // part_names.clear();
    }

    LOG_INFO << chunk_num * chunk_size  << " Sequences done!"; 

    return VoidToken();
  };

  // only on one rank, only once at the end of the pipeline
  auto finalize_pipe_func = [&]() -> void {
    LOG_INFO << "Output file: " << outdir + "epa_result.jplace";
    outfile << finalize_jplace_string(invocation);
    outfile.close();
  };



  if (options.prescoring) {
    auto pipe = make_pipeline(ingestion, perloop_prehook, init_pipe_func, finalize_pipe_func)
      .push(preplacement)
      .push(candidate_selection)
      .push(thorough_placement)
      .push(write_result);

    pipe.process();
  } else {
    auto pipe = make_pipeline(ingestion, perloop_prehook, init_pipe_func, finalize_pipe_func)
      .push(thorough_placement)
      .push(write_result);

    pipe.process();
  }
}

void simple_mpi(Tree& reference_tree, 
                const std::string& query_file, 
                const std::string& outdir,
                const Options& options,
                const std::string& invocation)
{
  // Timer<> flight_time;
  std::ofstream flight_file(outdir + "stat");

  std::string status_file_name(outdir + "pepa.status");
  std::ofstream trunc_status_file(status_file_name, std::ofstream::trunc);

  std::vector<std::string> part_names;


  const auto num_branches = reference_tree.nums().branches;

  // get all edges
  std::vector<pll_unode_t *> branches(num_branches);
  auto num_traversed_branches = utree_query_branches(reference_tree.tree(), &branches[0]);
  if (num_traversed_branches != num_branches) {
    throw std::runtime_error{"Traversing the utree went wrong during pipeline startup!"};
  }

  auto lookups = 
    std::make_shared<Lookup_Store>(num_branches, reference_tree.partition()->states);

  // some MPI prep
  int local_rank = 0;
  int num_ranks = 1;

  MPI_COMM_RANK(MPI_COMM_WORLD, &local_rank);
  MPI_COMM_SIZE(MPI_COMM_WORLD, &num_ranks);

  LOG_INFO << "Number of ranks: " << num_ranks;

  std::vector<int> all_ranks(num_ranks);
  for (int i = 0; i < num_ranks; ++i) {
    all_ranks[i] = i;
  }

  Binary_Fasta_Reader reader(query_file);

  // how many should each rank read?
  const size_t part_size = ceil(reader.num_sequences() / static_cast<double>(num_ranks));
  LOG_INFO << "Number of sequences per rank: " << part_size;

  // read only the locally relevant part of the queries
  // ... by skipping the appropriate amount
  const size_t local_rank_seq_offset = part_size * local_rank;
  reader.skip_to_sequence( local_rank_seq_offset );
  // and limiting the reading to the given window
  reader.constrain(part_size);

  size_t num_sequences = options.chunk_size;
  Work all_work(std::make_pair(0, num_branches), std::make_pair(0, num_sequences));
  Work blo_work;

  size_t chunk_num = 1;

  using Sample = Sample<Placement>;
  Sample result;
  MSA chunk;
  size_t sequences_done = 0; // not just for info output!
  while ( (num_sequences = reader.read_next(chunk, options.chunk_size) ) ) {

    assert(chunk.size() == num_sequences);

    LOG_DBG << "num_sequences: " << num_sequences << std::endl;

    const size_t seq_id_offset = sequences_done + local_rank_seq_offset;

    if (num_sequences < options.chunk_size) {
      all_work = Work(std::make_pair(0, num_branches), std::make_pair(0, num_sequences));
    }

    if (options.prescoring) {

      Sample preplace;

      LOG_DBG << "Preplacement." << std::endl;
      place(all_work,
            chunk,
            reference_tree,
            branches,
            preplace,
            false,
            options,
            lookups);

      // Candidate Selection
      LOG_DBG << "Selecting candidates." << std::endl;
      compute_and_set_lwr(preplace);

      if (options.prescoring_by_percentage) {
        discard_bottom_x_percent(preplace, 
                                (1.0 - options.prescoring_threshold));
      } else {
        discard_by_accumulated_threshold( preplace, 
                                          options.prescoring_threshold,
                                          options.filter_min,
                                          options.filter_max);
      }

      blo_work = Work(preplace);

    } else {
      blo_work = all_work;
    }

    Sample blo_sample;

    // BLO placement
    LOG_DBG << "BLO Placement." << std::endl;
    place(blo_work,
          chunk,
          reference_tree,
          branches,
          blo_sample,
          true,
          options,
          lookups,
          seq_id_offset);

    // Output
    compute_and_set_lwr(blo_sample);
    if (options.acc_threshold) {
      LOG_DBG << "Filtering by accumulated threshold: " << options.support_threshold << std::endl;
      discard_by_accumulated_threshold( blo_sample, 
                                        options.support_threshold,
                                        options.filter_min,
                                        options.filter_max);
    } else {
      LOG_DBG << "Filtering placements below threshold: " << options.support_threshold << std::endl;
      discard_by_support_threshold( blo_sample,
                                    options.support_threshold,
                                    options.filter_min,
                                    options.filter_max);
    }

    merge(result, std::move(blo_sample));

    sequences_done += num_sequences;
    LOG_INFO << sequences_done  << " Sequences done!";
    ++chunk_num;
  }

#ifdef __MPI
  // send to output: on rank <designated_writer> 
  LOG_DBG << "Gathering results on Rank " << 0;
  Timer<> dummy;
  epa_mpi_gather(result, 0, all_ranks, local_rank, dummy);
#endif //__MPI

  if (local_rank == 0) {
    // create output file
    LOG_INFO << "Output file: " << outdir + "epa_result.jplace";
    std::ofstream outfile;
    outfile.open(outdir + "epa_result.jplace");
    outfile << init_jplace_string(
      get_numbered_newick_string(reference_tree.tree()));
    outfile << sample_to_jplace_string(result);
    outfile << finalize_jplace_string(invocation);
    outfile.close();
  }

  MPI_BARRIER(MPI_COMM_WORLD);
}

