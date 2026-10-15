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
