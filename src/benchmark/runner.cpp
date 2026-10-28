#include "benchmark/runner.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

#include "backends/locking_backend.h"
#include "backends/mvcc_backend.h"
#include "benchmark/metrics.h"
#include "benchmark/zipfian.h"

namespace benchmark {
namespace {

using Clock = std::chrono::steady_clock;

std::string Key(int64_t i) { return "key:" + std::to_string(i); }

std::unique_ptr<KVBackend> MakeBackend(const std::string& name) {
  if (name == "locking") return std::make_unique<backends::LockingBackend>();
  if (name == "mvcc") return std::make_unique<backends::MVCCBackend>();
  throw std::runtime_error("unknown backend '" + name + "'");
}

// Simple reusable (cyclic) barrier — the project targets C++17, so
// std::barrier (C++20) isn't available. Used once here, to bring every
// worker into the measured phase together (requirement: a synchronization
// barrier so worker threads begin the measured phase consistently).
class Barrier {
 public:
  explicit Barrier(size_t count) : count_(count), initial_(count) {}

  void Wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    size_t gen = generation_;
    if (--count_ == 0) {
      count_ = initial_;
      ++generation_;
      cv_.notify_all();
    } else {
      cv_.wait(lock, [&] { return gen != generation_; });
    }
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  size_t count_;
  size_t initial_;
  size_t generation_ = 0;
};

// Latency samples retained per worker. Fixed on purpose: recording one
// sample per operation made the harness's own memory grow with throughput,
// which contaminated the reported RSS — a faster backend was charged for a
// bigger latency log and so looked like it used more memory. With a fixed
// reservoir the harness overhead is a constant (threads x this x 8 bytes),
// identical for both backends, so memory_mb reflects the backend under
// test. 64Ki samples is far more than p99 needs to be stable.
constexpr size_t kLatencySamplesPerWorker = 65536;

struct WorkerResult {
  uint64_t ops = 0;
  uint64_t successes = 0;
  std::vector<uint64_t> latencies_us;  // reservoir sample, not every op
};

void RunWorker(KVBackend& backend, const BenchmarkConfig& cfg, int worker_index, Barrier& barrier,
               WorkerResult* out) {
  // Independent, deterministically-derived RNG state per worker so threads
  // never contend on (or share the sequence of) a single global RNG.
  ZipfianGenerator zipf(cfg.keyspace, cfg.zipf_theta,
                        cfg.seed ^ (static_cast<uint64_t>(worker_index) * 0x9E3779B97F4A7C15ULL));
  std::mt19937_64 op_rng(cfg.seed + static_cast<uint64_t>(worker_index) * 104729ULL + 7);
  // Separate stream for reservoir sampling so that recording latencies
  // cannot perturb the workload sequence itself.
  std::mt19937_64 sample_rng(cfg.seed + static_cast<uint64_t>(worker_index) * 15485863ULL + 11);
  std::uniform_int_distribution<int> pct_dist(1, 100);
  const std::string write_value(32, 'v');

  auto DoOp = [&]() -> bool {
    int64_t key_idx = zipf.Next();
    std::string key = Key(key_idx);
    if (pct_dist(op_rng) <= cfg.read_percent) {
      KVResult r = backend.Get(key);
      return r.status != KVStatus::kError;
    }
    KVResult r = backend.Set(key, write_value);
    return r.status == KVStatus::kSetOk;
  };

  // Allocate the sample buffer up front, before the measured phase, so the
  // allocation cost isn't attributed to measured latency and the memory is
  // already resident when RSS is sampled.
  out->latencies_us.reserve(kLatencySamplesPerWorker);

  auto warmup_end = Clock::now() + std::chrono::duration<double>(cfg.warmup_seconds);
  while (Clock::now() < warmup_end) DoOp();

  barrier.Wait();

  auto measure_end = Clock::now() + std::chrono::duration<double>(cfg.duration_seconds);
  while (Clock::now() < measure_end) {
    auto t0 = Clock::now();
    bool ok = DoOp();
    auto t1 = Clock::now();
    auto latency_us =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

    // Reservoir sampling (Algorithm R): every operation is equally likely
    // to end up in the retained sample, so the percentile is representative
    // of the whole measured window rather than just its first N operations.
    if (out->latencies_us.size() < kLatencySamplesPerWorker) {
      out->latencies_us.push_back(latency_us);
    } else {
      std::uniform_int_distribution<uint64_t> slot(0, out->ops);
      uint64_t j = slot(sample_rng);
      if (j < kLatencySamplesPerWorker) out->latencies_us[static_cast<size_t>(j)] = latency_us;
    }

    ++out->ops;
    if (ok) ++out->successes;
  }
}

}  // namespace

bool RunCorrectnessChecks(KVBackend& backend, std::string* error) {
  if (backend.Get("__bench_correctness_missing__").status != KVStatus::kGetNotFound) {
    *error = "GET of a never-set key did not return kGetNotFound";
    return false;
  }
  if (backend.Set("__bench_correctness_key__", "v1").status != KVStatus::kSetOk) {
    *error = "SET did not return kSetOk";
    return false;
  }
  KVResult r = backend.Get("__bench_correctness_key__");
  if (r.status != KVStatus::kGetFound || r.value != "v1") {
    *error = "GET after SET did not return the value that was just set";
    return false;
  }
  if (backend.Del("__bench_correctness_missing_2__").status != KVStatus::kDelNotFound) {
    *error = "DEL of a never-set key did not return kDelNotFound";
    return false;
  }
  return true;
}

BenchmarkResult RunBenchmark(const BenchmarkConfig& cfg) {
  std::string error;
  if (!ValidateConfig(cfg, &error)) throw std::runtime_error("invalid config: " + error);

  std::unique_ptr<KVBackend> backend = MakeBackend(cfg.backend);

  if (!RunCorrectnessChecks(*backend, &error)) {
    throw std::runtime_error("correctness check failed: " + error);
  }

  for (int64_t i = 0; i < cfg.keyspace; ++i) {
    backend->Set(Key(i), "initial-value");
  }

  Barrier barrier(static_cast<size_t>(cfg.threads));
  std::vector<WorkerResult> results(static_cast<size_t>(cfg.threads));
  std::vector<std::thread> threads;
  threads.reserve(static_cast<size_t>(cfg.threads));
  for (int t = 0; t < cfg.threads; ++t) {
    threads.emplace_back(RunWorker, std::ref(*backend), std::cref(cfg), t, std::ref(barrier),
                          &results[static_cast<size_t>(t)]);
  }
  for (auto& th : threads) th.join();  // wait for every worker to stop before aggregating

  // Sample RSS here: after every worker has stopped (so the backend is at
  // its steady-state size) but before the aggregation below allocates the
  // merged latency vector, so the reading reflects the backend plus a
  // fixed, backend-independent harness overhead.
  MemoryReading mem = ReadProcessResidentMemory();
  if (!mem.ok) throw std::runtime_error("memory measurement unavailable (/proc/self/status)");

  uint64_t total_ops = 0;
  uint64_t total_successes = 0;
  std::vector<uint64_t> all_latencies;
  for (auto& wr : results) {
    total_ops += wr.ops;
    total_successes += wr.successes;
    all_latencies.insert(all_latencies.end(), wr.latencies_us.begin(), wr.latencies_us.end());
  }

  // Every operation either completes or indicates a backend error; neither
  // backend should ever produce the latter. If one did, the run's numbers
  // are not trustworthy, so fail loudly instead of quietly publishing a
  // throughput figure that counts failed operations.
  if (total_successes != total_ops) {
    throw std::runtime_error("benchmark aborted: " + std::to_string(total_ops - total_successes) +
                             " of " + std::to_string(total_ops) +
                             " operations reported a backend error");
  }

  BenchmarkResult result;
  result.config = cfg;
  result.operations = total_ops;
  result.throughput_ops_sec =
      cfg.duration_seconds > 0.0 ? static_cast<double>(total_ops) / cfg.duration_seconds : 0.0;
  result.p99_latency_us = PercentileP99(&all_latencies);
  result.memory_mb = mem.resident_mb;

  if (auto* mvcc = dynamic_cast<backends::MVCCBackend*>(backend.get())) {
    result.abort_count = mvcc->AbortCount();
    result.abort_rate =
        total_ops > 0 ? static_cast<double>(result.abort_count) / static_cast<double>(total_ops)
                      : 0.0;
  } else {
    result.abort_count = 0;
    result.abort_rate = 0.0;  // N/A for the locking backend, reported as zero per the CSV schema
  }

  return result;
}

}  // namespace benchmark
