# ML-DSA constant-time triage

This records the ML-DSA work for [issue #2691](https://github.com/open-quantum-safe/liboqs/issues/2691).
The issue tracks multiple algorithm families; this report covers ML-DSA only.
The starting liboqs revision is `98e0db3c28d5246076fca81e8753457c765c70d5`,
which imports mldsa-native at
`834a90d5e846ffa1e1611bd24e160bb2e9b86d35`.

## Scope

The tests exercise key generation, signing, and signing with a 255-byte context
where supported, through liboqs's `test_sig` binary. All three parameter sets
and their external-message-representative variants are included:

- `ML-DSA-44`, `ML-DSA-65`, and `ML-DSA-87`.
- `ML-DSA-44-extmu`, `ML-DSA-65-extmu`, and `ML-DSA-87-extmu`.

The constant-time test build deliberately omits signature verification and
extended correctness tests because verification has no secret input. Its final
"verification passes as expected" message is not evidence that verification
ran in this configuration. Functional tests must use a separate build.

## Valgrind-Varlat results

All 192 executions passed with **zero errors and zero suppressed errors**.
No algorithm suppression files were supplied. Each row below covers all six
variants, both `generic` and `haswell` targets, and GCC 14.2.0 and Clang 19.1.7:

| Optimization | Executions | Unclassified warnings |
| --- | ---: | ---: |
| `-O0` | 24 | 0 |
| `-O1` | 24 | 0 |
| `-O2` | 24 | 0 |
| `-O3` | 24 | 0 |
| `-Os` | 24 | 0 |
| `-Ofast` | 24 | 0 |
| `-O2` without loop vectorization | 24 | 0 |
| `-O3` without loop vectorization | 24 | 0 |

The vectorization flags are `-fno-tree-vectorize` for GCC and `-fno-vectorize`
for Clang, matching the existing workflow. Builds use `Debug`,
`OQS_DIST_BUILD=OFF`, `OQS_USE_OPENSSL=OFF`, and
`OQS_ENABLE_TEST_CONSTANT_TIME_VALGRIND=ON`. The `haswell` configurations
select the x86_64 ML-DSA backend, including its AVX2, BMI2, and POPCNT code.
The test host is an Intel Xeon Platinum 8573C running x86_64 Linux.

The patched Valgrind version is `3.26.0.GIT` at upstream commit
`112f1080b7c21e37dfce0a2e589d0dc7aa115afa`, with the three patches prescribed
by the framework's Valgrind-Varlat README. Its variable-latency self-test
reported the expected secret-dependent integer-division warning before the
algorithm runs.

For example, reproduce one unsuppressed configuration with:

```sh
cmake -S . -B build_mldsa_varlat -GNinja \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS=-O2 -DOQS_OPT_TARGET=haswell \
  -DOQS_DIST_BUILD=OFF -DOQS_USE_OPENSSL=OFF \
  -DOQS_ENABLE_TEST_CONSTANT_TIME_VALGRIND=ON \
  -DOQS_MINIMAL_BUILD=SIG_ml_dsa
cmake --build build_mldsa_varlat --target test_sig --parallel 2
for alg in ML-DSA-44 ML-DSA-65 ML-DSA-87 \
           ML-DSA-44-extmu ML-DSA-65-extmu ML-DSA-87-extmu; do
  valgrind_varlat --tool=memcheck --variable-latency-errors=yes \
    --gen-suppressions=all --error-exitcode=123 \
    --max-stackframe=20480000 --num-callers=20 --track-origins=yes \
    build_mldsa_varlat/tests/test_sig "$alg" || exit "$?"
done
```

## MemorySanitizer annotation mismatch

The liboqs integration enables `MLD_CONFIG_CT_TESTING_ENABLED` for both testing
backends, but the imported `mldsa/src/sys.h` implements the native annotations
only as Valgrind client requests. Those requests have no effect on
MemorySanitizer's shadow memory.

With Clang 19.1.7, `OQS_OPT_TARGET=generic`, and `-O1`, an unsuppressed baseline
run reports `mld_rej_eta_c` in `mldsa/src/poly_kl.c`. That function already
marks the candidate's acceptance bit public immediately before branching on
it. MemorySanitizer still considers the bit secret because it never receives
the corresponding unpoison request. Public-matrix expansion in
`mld_rej_uniform_c` similarly inherits the poisoned public `rho` seed.

These are annotation-backend mismatches, not newly identified timing leaks.
The existing upstream annotations distinguish the public matrix seed and the
allowed rejection decisions from the accepted secret coefficients. The change
keeps those decisions and their byte ranges intact:

- Under MemorySanitizer, `MLD_CT_TESTING_SECRET` calls `__msan_poison` and
  `MLD_CT_TESTING_DECLASSIFY` calls `__msan_unpoison`.
- Under Valgrind, the existing client requests remain in use.
- Without constant-time testing, the existing no-op definitions remain in use.

The backend is detected using Clang's `__has_feature(memory_sanitizer)`, within
the existing constant-time testing guard. The implementation therefore does
not introduce a liboqs-specific dependency into the upstream core header.

## Inline assembly value barriers

Correcting the annotations exposes a second detector limitation. The native
constant-time helpers use an empty inline assembly expression as a compiler
value barrier:

```c
__asm__ volatile("" : "+r"(b));
```

MemorySanitizer checks whether the input to this opaque expression is
initialized. A secret input therefore produces a warning even though the
expression emits no algorithm instruction. In the Clang 19.1.7 `-O1` binary,
`PQCP_MLDSA_NATIVE_MLDSA44_C_poly_caddq` tests the shadow register and branches to
`__msan_warning_with_origin` at the barrier. This is sanitizer-generated
control flow, not a secret-dependent branch in the uninstrumented routine.
The same pattern occurs in `mld_poly_chknorm_c`.

For MemorySanitizer constant-time builds, the patch selects mldsa-native's
existing `MLD_CONFIG_NO_ASM_VALUE_BARRIER` fallback. That fallback XORs with a
volatile zero value, preserves the value barrier, and lets MemorySanitizer
track secrecy through ordinary C operations. No signing, NTT, sampling, or
comparison function is added to an ignorelist.

This fallback changes which compiler barrier is tested under MemorySanitizer.
Valgrind testing is still needed to examine the normal compiled implementation.
Preprocessing the original and patched headers with both GCC and Clang gives
identical output for ordinary builds and Valgrind constant-time builds.

## Recorded MemorySanitizer results

These runs used Debian Clang 19.1.7 targeting `x86_64-pc-linux-gnu`, the generic
C backend, and the build options below. Each configuration ran all six variants
once. All 42 executions exited successfully with zero MemorySanitizer warnings.
No ignorelist files were supplied.

| Optimization flags | Standard variants | External-message-representative variants | Warnings |
| --- | --- | --- | --- |
| `-O1` | 3 passed | 3 passed | 0 |
| `-O2` | 3 passed | 3 passed | 0 |
| `-O3` | 3 passed | 3 passed | 0 |
| `-Os` | 3 passed | 3 passed | 0 |
| `-Ofast` | 3 passed | 3 passed | 0 |
| `-O2 -fno-vectorize` | 3 passed | 3 passed | 0 |
| `-O3 -fno-vectorize` | 3 passed | 3 passed | 0 |

### Native MemorySanitizer experiment

A separate `OQS_OPT_TARGET=auto` build at `-O1` exercised the x86_64 backend.
All six variants exited with status 123 at the first warning in
`PQCP_MLDSA_NATIVE_MLDSA*_X86_64_poly_challenge`, at the rejection loop in
`poly_kl.c`. The challenge seed has already been declassified by the caller;
the loop reads the SHAKE output derived from that public seed.

A separate SHAKE shadow-memory probe isolated the shared primitive. It hashes
32 public zero bytes into a 32-byte buffer whose shadow was explicitly
poisoned before the call. Both builds produce the same hash bytes. The generic
build leaves the entire output defined, while the optimized build leaves its
first byte poisoned. The optimized SHAKE assembly writes the actual bytes
without updating MemorySanitizer's shadow memory.

These six native failures remain unsuppressed and are excluded from the 42
passing generic results. The recorded MemorySanitizer coverage is therefore
the generic C configuration. Native arithmetic and shared assembly need the
separate Valgrind-Varlat checks; ignoring the entire challenge function would
hide the instrumentation boundary.

## Reproducing the MemorySanitizer runs

The generic build uses no suppression files and no manually supplied
`MLD_CONFIG_*` flags:

```sh
cmake -S . -B build_mldsa_memsan -GNinja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DOQS_MINIMAL_BUILD=SIG_ml_dsa_44;SIG_ml_dsa_65;SIG_ml_dsa_87;SIG_ml_dsa_44_extmu;SIG_ml_dsa_65_extmu;SIG_ml_dsa_87_extmu' \
  -DOQS_OPT_TARGET=generic \
  -DOQS_DIST_BUILD=OFF \
  -DOQS_USE_OPENSSL=OFF \
  -DBUILD_SHARED_LIBS=ON \
  -DOQS_ENABLE_TEST_CONSTANT_TIME_MEMSAN=ON \
  '-DCMAKE_C_FLAGS=-fsanitize=memory -fsanitize-recover=all -O1 -g' \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=memory \
  -DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=memory
cmake --build build_mldsa_memsan --parallel 2

export MSAN_OPTIONS=halt_on_error=1:exit_code=123:symbolize=1
export MSAN_SYMBOLIZER_PATH="$(command -v llvm-symbolizer)"
for alg in ML-DSA-44 ML-DSA-65 ML-DSA-87 \
           ML-DSA-44-extmu ML-DSA-65-extmu ML-DSA-87-extmu; do
  build_mldsa_memsan/tests/test_sig "$alg" || exit "$?"
done
```

Use a separate build directory for each optimization setting. The settings
match the generic MemorySanitizer portion of `ct-tooling.yml`: `-O1`, `-O2`,
`-O3`, `-Os`, `-Ofast`, `-O2 -fno-vectorize`, and `-O3 -fno-vectorize`.
The workflow's script can also run one variant directly, for example:

```sh
cd tests/ct_tooling
./ct_test.sh memsan clang generic -O1 ML-DSA-44
```

## Annotation regression probe

The [annotation regression test](../test_mldsa_ct_annotations.c) checks actual
MemorySanitizer shadow state independently of signing output. It verifies that
classification poisons exactly the requested byte, declassification leaves
neighboring bytes secret, and reclassification restores secrecy.

The MemorySanitizer workflow runs this probe before signing tests for ML-DSA,
using each matrix optimization setting. It includes the reference ML-DSA-44
header; all nine imported header copies are identical after regeneration.
Run it directly from the repository root with:

```sh
clang -fsanitize=memory -O1 -g -fno-pie -no-pie \
  -Isrc/sig/ml_dsa/mldsa-native_ml-dsa-44_ref/mldsa/src \
  tests/ct_tooling/test_mldsa_ct_annotations.c -o /tmp/mldsa_ct_annotations
MSAN_OPTIONS=halt_on_error=1:exit_code=123 /tmp/mldsa_ct_annotations
```

The probe passes at all seven MemorySanitizer optimization settings. Compiling
the same source against the original header produces exit status 1 at the
classification check. An explicitly supplied value-barrier override also
passes with strict compiler warnings enabled.

A separate negative control initializes a volatile byte, classifies it with
`MLD_CT_TESTING_SECRET`, and branches on that byte. With
`MSAN_OPTIONS=halt_on_error=1:exit_code=123`, the original header lets the
program exit successfully without a warning, while the patched header reports
the secret-dependent branch and exits with status 123. This verifies that
classification still detects forbidden uses, alongside the clean signing
runs and the bounded declassification checks.

## Functional and isolated-branch checks

A separate ordinary build used GCC 14.2.0, `-O2`, `OQS_OPT_TARGET=generic`,
strict compiler warnings, and no constant-time testing option. All six
`test_sig` cases and the three available standard-variant known-answer tests
passed. The complete pytest invocation reported 12 passed, four skipped,
and 796 deselected:

```sh
OQS_BUILD_DIR=build_mldsa_functional python3 -m pytest -q \
  tests/test_cmdline.py tests/test_kat.py \
  --algs ML-DSA-44,ML-DSA-65,ML-DSA-87,ML-DSA-44-extmu,ML-DSA-65-extmu,ML-DSA-87-extmu
```

The count includes three example-program checks. The repository intentionally
skips known-answer tests for the three external-message-representative
variants; the fourth skip is an unrelated disabled stateful-signature check.
These skips are not counted as cryptographic validation.

The full compiler matrices above were recorded before separating this
ML-DSA change from the wider triage work. The isolated branch's ML-DSA
implementation, common primitives, relevant CMake inputs, and `test_sig`
harness were compared byte for byte with those validated inputs. The branch
has no dependency on the wider runner or algorithm-classification changes.

Fresh builds of this branch also run all six variants with generic Clang
MemorySanitizer at `-O1` and native Haswell GCC Valgrind-Varlat at `-O2`,
without custom suppression files or ignorelists. These executions are
additional integration checks, separate from the 192 and 42 matrix totals.

## Limits

These dynamic checks cover the executed paths and inputs. They are not a
constant-time proof, an analysis of every target architecture, or a
microarchitectural timing measurement. Existing upstream declassifications
remain part of the test's trust boundary.

In particular, mldsa-native's native rejection samplers temporarily declassify
the random candidate bytes and reclassify the accepted coefficient outputs.
The assembly review below covers that boundary for the tested x86_64 backend.
A clean dynamic result by itself does not establish that property.

MemorySanitizer cannot instrument the native assembly backend or all shared
assembly primitives. A clean generic result must not be presented as a result
for optimized x86_64 or AArch64 code. Use Valgrind-Varlat for the tested native
x86_64 configuration and report that configuration separately.

## Native rejection-sampler assembly review

The x86_64 integration's `mld_rej_uniform_eta2_native` and
`mld_rej_uniform_eta4_native` wrappers declassify the candidate byte buffer
before calling assembly and mark the accepted output coefficients secret
again afterward. The reviewed routines are
`mldsa_rej_uniform_eta2_avx2_asm.S` and
`mldsa_rej_uniform_eta4_avx2_asm.S`, under
`mldsa/src/native/x86_64/src/` in the imported implementation. The linked
`test_sig` binaries were also disassembled, including their local loop labels.
Across all 16 native compiler and optimization configurations, the eta-2
routine has identical 543-byte machine code in ML-DSA-44 and ML-DSA-87.
The eta-4 routine has identical 403-byte machine code in ML-DSA-65 across
those configurations.

Each candidate is a four-bit nibble. For eta 2, acceptance means `n < 15`
and the coefficient is `2 - (n % 5)`. The 15 accepted nibbles have exactly
three representatives of each remainder modulo 5. For eta 4, acceptance
means `n < 9` and the coefficient is `4 - n`. Conditional on acceptance,
the coefficients are uniform in their respective ranges. The acceptance
pattern of independent random candidates therefore does not distinguish the
values of the accepted coefficients. This is the rejection-sampling property
used by the existing upstream declassification.

The assembly respects that distinction:

| Operation | Reviewed data dependence |
| --- | --- |
| Vector candidate extraction | Fixed loads, shifts, and masks produce the nibbles. |
| Compaction-table loads | `vpsubb` followed by `vpmovmskb` produces an acceptance mask. Only eight bits of that mask index each table row through `(%rdx,%r10,8)`. The coefficient value does not index the table. |
| Output locations | `popcntl` counts accepted candidates. This count advances the output pointer, so locations depend on the allowed acceptance pattern. |
| Vector loop and scalar-tail selection | Comparisons use the consumed-byte count and accepted-output count against fixed buffer and output limits. |
| Scalar rejection branches | `cmp` and `jae` compare a nibble only with 15 or 9, respectively. Other scalar branches check input and output counts. |
| Accepted-value conversion | Eta 2 uses fixed immediate multiplication, shifts, and subtraction to reduce modulo 5. Eta 4 uses subtraction. Neither routine uses integer division or a coefficient-indexed memory lookup. |

For example, the eta-2 scalar tail tests `n >= 15` once and, on acceptance,
computes the remainder using `imul $0xcd`, `shr $0xa`, `imul $0x5`, and
subtraction. The arithmetic result is written at the next accepted-output
index; it does not choose a subsequent branch or address. The vector path
uses fixed instruction sequences and register shuffles for the same mapping.

This review establishes the branch and address dependencies of these two
assembled routines under the stated declassification policy. It is not a
claim about power leakage, speculative execution, or the instruction latency
of every processor that might execute the code. AArch64 samplers were not
executed or audited here.
