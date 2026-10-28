# LockingBackend — ThreadSanitizer verification

Build command used (separate build directory, TSan-instrumented):

```bash
cmake -S . -B build-tsan -G Ninja \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build build-tsan
```

Run command:

```bash
ctest --test-dir build-tsan --output-on-failure
./build-tsan/tests/locking/locking_backend_test
./build-tsan/tests/locking/locking_concurrency_test
```

## Result

All three tests (`kv_backend_test`, `locking_backend_test`, `locking_concurrency_test`) pass under this build. ThreadSanitizer reported zero data races, zero use-after-free, and zero synchronization/lifetime errors across:

- `TestConcurrentSetGetDelSameKey` — 8 threads hammering SET/GET/DEL/EXPIRE on one shared key.
- `TestConcurrentDifferentStripes` — 8 threads writing/reading disjoint keys concurrently.
- `TestExpireRacingWithGetSet` — dedicated EXPIRE, SET, and GET threads racing on one key for 200ms continuously.
- `TestRandomizedStress` — 8 threads x 5,000 randomized SET/GET/DEL/EXPIRE operations over a 64-key keyspace.

No code changes were required as a result of this verification pass — the striped read/write locking design was race-free on the first TSan run.
