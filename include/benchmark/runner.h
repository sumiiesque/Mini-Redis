#pragma once

#include <string>

#include "benchmark/config.h"
#include "benchmark/result.h"
#include "kv_backend.h"

namespace benchmark {

// Runs correctness checks against a freshly-constructed, empty `backend`:
// SET a key then GET it back, GET a never-set key (must be missing), DEL a
// never-set key (must be well-defined, not an error). Returns true iff all
// checks pass; on failure, `error` explains which one didn't. Intended to
// run once, before the pre-populate/warmup/measure phases, so a broken
// backend never gets to the point of producing a misleading result row.
bool RunCorrectnessChecks(KVBackend& backend, std::string* error);

// Runs one full benchmark: constructs the backend named in cfg.backend,
// pre-populates cfg.keyspace keys, runs correctness checks, then executes
// cfg.threads worker threads through a warmup phase (cfg.warmup_seconds,
// unmeasured) synchronized via a barrier into a measured phase
// (cfg.duration_seconds) of GET/SET operations against a Zipfian-sampled
// keyspace, then aggregates per-worker results.
//
// Every worker uses the identical code path (same KVBackend interface,
// same workload generator, same timing methodology) regardless of which
// backend was selected — the only per-backend difference is which concrete
// KVBackend subclass gets constructed.
//
// Throws std::runtime_error if cfg fails ValidateConfig, if correctness
// checks fail, or if memory measurement is unavailable (never silently
// substitutes a zero).
BenchmarkResult RunBenchmark(const BenchmarkConfig& cfg);

}  // namespace benchmark
