# ML-DSA Valgrind-Varlat triage

This covers the ML-DSA item in [#2691](https://github.com/open-quantum-safe/liboqs/issues/2691).

## Findings

Focused unsuppressed GCC Haswell `-O2` runs reported zero Valgrind-Varlat
errors across all six variants on the fork base `98e0db3c` and upstream
base `3ad27a39`.

The x86_64 eta samplers declassify the candidate buffer and reclassify the
accepted coefficients. The reviewed assembly uses the acceptance mask to
index the compaction table and its population count to advance the output
position. Scalar branches check bounds and accept nibbles below 15 (eta 2)
or 9 (eta 4). Each accepted coefficient has the same number of preimages
for a uniformly distributed nibble, so the acceptance pattern leaves the
distribution of accepted values uniform.

MSan exposed gaps in shadow tracking at the imported Valgrind annotations,
inline assembly value barriers, and optimized SHAKE. The import-side MSan
work is tracked in [mldsa-native #1563](https://github.com/pq-code-package/mldsa-native/pull/1563).

## Results

The direct Linux matrix at `b5682fe0` passed **192/192 cases**, with zero
errors and zero suppressed errors. It covered ML-DSA-44, ML-DSA-65, and ML-DSA-87
and their extmu variants with GCC and Clang on `generic` and `haswell`.
Each compiler used `-O0`, `-O1`, `-O2`, `-O3`, `-Os`, `-Ofast`, and
`-O2`/`-O3` with loop vectorization disabled.

All three extmu variants also passed representative Linux Valgrind-Varlat
runs through `ct_test.sh` with the individual-build guard fix in
`vectors_sig.c`. Hosted CI is pending. Dynamic coverage applies to
the executed paths and build configurations.

## Reproduce

Run this representative workflow case from the repository root.

```sh
(cd tests/ct_tooling && ./ct_test.sh valgrind-varlat gcc haswell -O2 ML-DSA-44)
```

Build and execute `test_sig` directly for an unsuppressed run.

```sh
cmake -S . -B build_ct_varlat -GNinja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_C_FLAGS=-O2 -DOQS_OPT_TARGET=haswell \
  -DOQS_DIST_BUILD=OFF -DOQS_USE_OPENSSL=OFF -DOQS_MINIMAL_BUILD=SIG_ml_dsa_44 \
  -DOQS_ENABLE_TEST_CONSTANT_TIME_VALGRIND=ON
cmake --build build_ct_varlat --target test_sig
valgrind_varlat --tool=memcheck --variable-latency-errors=yes \
  --error-exitcode=123 --max-stackframe=20480000 --track-origins=yes \
  build_ct_varlat/tests/test_sig ML-DSA-44
```
