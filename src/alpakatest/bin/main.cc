#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

#include <tbb/global_control.h>
#include <tbb/info.h>
#include <tbb/task_arena.h>

#include "AlpakaCore/alpakaConfig.h"
#include "AlpakaCore/backend.h"
#include "AlpakaCore/initialise.h"
#include "EventProcessor.h"
#include "PosixClockGettime.h"

namespace {
  void print_help(std::string const& name) {
    std::cout << name << ": "
#ifdef ALPAKA_ACC_CPU_B_SEQ_T_SEQ_PRESENT
              << "[--serial] "
#endif
#ifdef ALPAKA_ACC_CPU_B_TBB_T_SEQ_PRESENT
              << "[--tbb] "
#endif
#ifdef ALPAKA_ACC_GPU_CUDA_PRESENT
              << "[--cuda] "
#endif
#ifdef ALPAKA_ACC_GPU_HIP_PRESENT
              << "[--hip] "
#endif
              << "[--numberOfThreads NT] [--numberOfStreams NS] [--maxEvents ME] [--data PATH] "
                 "[--transfer]\n\n"
              << "Options\n"
#ifdef ALPAKA_ACC_CPU_B_SEQ_T_SEQ_PRESENT
              << " --serial            Use CPU Serial backend\n"
#endif
#ifdef ALPAKA_ACC_CPU_B_TBB_T_SEQ_PRESENT
              << " --tbb               Use CPU TBB backend\n"
#endif
#ifdef ALPAKA_ACC_GPU_CUDA_PRESENT
              << " --cuda              Use CUDA backend\n"
#endif
#ifdef ALPAKA_ACC_GPU_HIP_PRESENT
              << " --hip               Use ROCm/HIP backend\n"
#endif
              << " --numberOfThreads   Number of threads to use (default 1, use 0 to use all CPU cores)\n"
              << " --numberOfStreams   Number of concurrent events (default 0 = numberOfThreads)\n"
              << " --maxEvents         Number of events to process (default -1 for all events in the input file)\n"
              << " --runForMinutes     Continue processing the set of 1000 events until this many minutes have passed "
                 "(default -1 for disabled; conflicts with --maxEvents)\n"
              << " --inputFile         Path to the input file (default 'data/input/raw2D.bin' in the directory of "
                 "the executable)\n"
              << " --transfer          Transfer results from GPU to CPU (default is to leave them on GPU)\n"
              << " --validation        Run (rudimentary) validation at the end (implies --transfer)\n"
              << " --empty             Ignore all producers (for testing only)\n"
              << std::endl;
  }
}  // namespace

bool getOptionalArgument(std::vector<std::string> const& args, std::vector<std::string>::iterator& i, int& value) {
  auto it = i;
  ++it;
  if (it == args.end()) {
    return false;
  }
  try {
    value = std::stoi(*it);
    ++i;
    return true;
  } catch (...) {
    return false;
  }
}

bool getOptionalArgument(std::vector<std::string> const& args, std::vector<std::string>::iterator& i, float& value) {
  auto it = i;
  ++it;
  if (it == args.end()) {
    return false;
  }
  try {
    value = std::stof(*it);
    ++i;
    return true;
  } catch (...) {
    return false;
  }
}

bool getOptionalArgument(std::vector<std::string> const& args,
                         std::vector<std::string>::iterator& i,
                         std::filesystem::path& value) {
  auto it = i;
  ++it;
  if (it == args.end()) {
    return false;
  }
  value = *it;
  return true;
}

template <typename T>
void getArgument(std::vector<std::string> const& args, std::vector<std::string>::iterator& i, T& value) {
  if (not getOptionalArgument(args, i, value)) {
    std::cerr << "error: " << *i << " expects an argument" << std::endl;
    exit(EXIT_FAILURE);
  }
}

int main(int argc, char** argv) {
  // Parse command line arguments
  std::vector<std::string> args(argv, argv + argc);
  std::unordered_map<Backend, float> backends;
  int numberOfThreads = 1;
  int numberOfStreams = 0;
  int maxEvents = -1;
  int runForMinutes = -1;
  std::filesystem::path inputFile;
  bool transfer = false;
  bool validation = false;
  bool empty = false;
  for (auto i = args.begin() + 1, e = args.end(); i != e; ++i) {
    if (*i == "-h" or *i == "--help") {
      print_help(args.front());
      return EXIT_SUCCESS;
#ifdef ALPAKA_ACC_CPU_B_SEQ_T_SEQ_PRESENT
    } else if (*i == "--serial") {
      float weight = 1.;
      getOptionalArgument(args, i, weight);
      backends.insert_or_assign(Backend::SERIAL, weight);
#endif
#ifdef ALPAKA_ACC_CPU_B_TBB_T_SEQ_PRESENT
    } else if (*i == "--tbb") {
      float weight = 1.;
      getOptionalArgument(args, i, weight);
      backends.insert_or_assign(Backend::TBB, weight);
#endif
#ifdef ALPAKA_ACC_GPU_CUDA_PRESENT
    } else if (*i == "--cuda") {
      float weight = 1.;
      getOptionalArgument(args, i, weight);
      backends.insert_or_assign(Backend::CUDA, weight);
#endif
#ifdef ALPAKA_ACC_GPU_HIP_PRESENT
    } else if (*i == "--hip") {
      float weight = 1.;
      getOptionalArgument(args, i, weight);
      backends.insert_or_assign(Backend::HIP, weight);
#endif
    } else if (*i == "--numberOfThreads") {
      getArgument(args, i, numberOfThreads);
    } else if (*i == "--numberOfStreams") {
      getArgument(args, i, numberOfStreams);
    } else if (*i == "--maxEvents") {
      getArgument(args, i, maxEvents);
    } else if (*i == "--runForMinutes") {
      getArgument(args, i, runForMinutes);
    } else if (*i == "--inputFile") {
      getArgument(args, i, inputFile);
    } else if (*i == "--transfer") {
      transfer = true;
    } else if (*i == "--validation") {
      transfer = true;
      validation = true;
    } else if (*i == "--empty") {
      empty = true;
    } else {
      std::cout << "Invalid parameter " << *i << std::endl << std::endl;
      print_help(args.front());
      return EXIT_FAILURE;
    }
  }
  if (maxEvents >= 0 and runForMinutes >= 0) {
    std::cout << "Got both --maxEvents and --runForMinutes, please give only one of them" << std::endl;
    return EXIT_FAILURE;
  }
  if (numberOfThreads == 0) {
    numberOfThreads = tbb::info::default_concurrency();
  }
  if (numberOfStreams == 0) {
    numberOfStreams = numberOfThreads;
  }
  if (inputFile.empty()) {
    inputFile = std::filesystem::path(args[0]).parent_path() / "data" / "input" / "raw2D.bin";
  }
  if (not std::filesystem::exists(inputFile)) {
    std::cout << "Input file '" << inputFile << "' does not exist" << std::endl;
    return EXIT_FAILURE;
  }

  // Initialiase the selected backends
#ifdef ALPAKA_ACC_CPU_B_SEQ_T_SEQ_PRESENT
  if (backends.find(Backend::SERIAL) != backends.end()) {
    cms::alpakatools::initialise<alpaka_serial_sync::Platform>();
  }
#endif
#ifdef ALPAKA_ACC_CPU_B_TBB_T_SEQ_PRESENT
  if (backends.find(Backend::TBB) != backends.end()) {
    cms::alpakatools::initialise<alpaka_tbb_async::Platform>();
  }
#endif
#ifdef ALPAKA_ACC_GPU_CUDA_PRESENT
  if (backends.find(Backend::CUDA) != backends.end()) {
    cms::alpakatools::initialise<alpaka_cuda_async::Platform>();
  }
#endif
#ifdef ALPAKA_ACC_GPU_HIP_PRESENT
  if (backends.find(Backend::HIP) != backends.end()) {
    cms::alpakatools::initialise<alpaka_rocm_async::Platform>();
  }
#endif

  // Initialize EventProcessor
  std::vector<std::string> esmodules;
  edm::Alternatives alternatives;
  if (not empty) {
    // host-only ESModules
    esmodules = {"IntESProducer"};
    for (auto const& [backend, weight] : backends) {
      std::string prefix = "alpaka_" + name(backend) + "::";
      // "portable" EDModules
      std::vector<std::string> edmodules;
      edmodules.emplace_back(prefix + "TestProducer");
      edmodules.emplace_back(prefix + "TestProducer3");
      edmodules.emplace_back(prefix + "TestProducer2");
      if (transfer) {
        // add modules for transfer
      }
      if (validation) {
        // add modules for validation
      }
      alternatives.emplace_back(backend, weight, std::move(edmodules));
    }
  }
  edm::EventProcessor processor(
      maxEvents, runForMinutes, numberOfStreams, std::move(alternatives), std::move(esmodules), inputFile, validation);

  if (runForMinutes < 0) {
    std::cout << "Processing " << processor.maxEvents() << " events,";
  } else {
    std::cout << "Processing for about " << runForMinutes << " minutes,";
  }
  {
    std::cout << " with " << numberOfStreams << " concurrent events (";
    bool need_comma = false;
    for (auto const& [backend, streams] : processor.backends()) {
      if (need_comma) {
        std::cout << ", ";
      }
      std::cout << streams << " on " << backend;
      need_comma = true;
    }
    std::cout << ") and " << numberOfThreads << " threads." << std::endl;
  }

  // Initialize the TBB thread pool
  tbb::global_control tbb_max_threads{tbb::global_control::max_allowed_parallelism,
                                      static_cast<std::size_t>(numberOfThreads)};

  // Run work
  auto cpu_start = PosixClockGettime<CLOCK_PROCESS_CPUTIME_ID>::now();
  auto start = std::chrono::high_resolution_clock::now();
  try {
    tbb::task_arena arena(numberOfThreads);
    arena.execute([&] { processor.runToCompletion(); });
  } catch (std::runtime_error& e) {
    std::cout << "\n----------\nCaught std::runtime_error" << std::endl;
    std::cout << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch (std::exception& e) {
    std::cout << "\n----------\nCaught std::exception" << std::endl;
    std::cout << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch (...) {
    std::cout << "\n----------\nCaught exception of unknown type" << std::endl;
    return EXIT_FAILURE;
  }
  auto cpu_stop = PosixClockGettime<CLOCK_PROCESS_CPUTIME_ID>::now();
  auto stop = std::chrono::high_resolution_clock::now();

  // Run endJob
  try {
    processor.endJob();
  } catch (std::runtime_error& e) {
    std::cout << "\n----------\nCaught std::runtime_error" << std::endl;
    std::cout << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch (std::exception& e) {
    std::cout << "\n----------\nCaught std::exception" << std::endl;
    std::cout << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch (...) {
    std::cout << "\n----------\nCaught exception of unknown type" << std::endl;
    return EXIT_FAILURE;
  }

  // Work done, report timing
  auto diff = stop - start;
  auto time = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(diff).count()) / 1e6;
  auto cpu_diff = cpu_stop - cpu_start;
  auto cpu = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(cpu_diff).count()) / 1e6;
  maxEvents = processor.processedEvents();
  std::cout << "Processed " << maxEvents << " events in " << std::scientific << time << " seconds, throughput "
            << std::defaultfloat << (maxEvents / time) << " events/s, CPU usage per thread: " << std::fixed
            << std::setprecision(1) << (cpu / time / numberOfThreads * 100) << "%" << std::endl;
  return EXIT_SUCCESS;
}
