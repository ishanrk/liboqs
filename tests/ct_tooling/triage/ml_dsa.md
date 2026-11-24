# ML-DSA constant-time triage

This covers the ML-DSA item in [#2691](https://github.com/open-quantum-safe/liboqs/issues/2691).
The fork base `98e0db3c` and upstream base `3ad27a39` both import
mldsa-native at `834a90d5e846ffa1e1611bd24e160bb2e9b86d35`.

## Warning analysis

| Site | Source and compiler evidence | Disposition |
| --- | --- | --- |
| Portable `mld_rej_eta_c` and `mld_rej_uniform_c` | The branch in the eta sampler follows declassification of `sizeof(int)` at `&t_valid`. The matrix expander uses `rho`, the first 32 bytes of the expanded seed and part of the public key. The imported Valgrind client requests leave those ranges poisoned under MSan. | Route the existing ranges to `__msan_poison` and `__msan_unpoison` in MSan builds. |
| Portable `mld_poly_challenge` on the baseline | The branch and indexed accesses consume SHAKE output derived from the challenge seed. Signing marks that seed public immediately before sampling, but the Valgrind client request has no effect on MSan shadow. | The generic C SHAKE path propagates the defined shadow once declassification reaches MSan. |
| Portable `mld_poly_caddq` and `mld_poly_chknorm_c` with the original value barrier | The empty asm barrier emits no algorithm instruction. Clang inserts a shadow check and warning branch before passing the secret operand to the asm. The existing volatile-zero C fallback propagates shadow through the XOR. | Select the C barrier in MSan builds. Ordinary and Valgrind builds retain the asm barrier. |
| x86_64 `mld_poly_challenge` under MSan | Signing declassifies the public challenge seed (32, 48 or 64 bytes). The linked XKCP AVX2 SHAKE path writes the squeezed bytes in assembly, leaving their shadow poisoned. | Run weekly ML-DSA MSan on `generic`. Test the x86_64 backend with Varlat. |

With `-O1`, every variant on both bases first warned at eta acceptance.
The compiler shadow probe cleared the 32-byte `rho` and `sizeof(int)`
acceptance range only on the candidate, leaving the byte after `rho`
poisoned. With only the annotation change applied, every variant first
warned at the asm barrier in `poly_caddq`. The linked code branches to
`__msan_warning` there, while the C fallback loads the volatile blocker
and propagates the input shadow. A deliberately tainted branch reported
an error with the candidate annotations.

The Haswell diagnostic first warned at the challenge rejection loop in
all six variants. A public-input SHAKE256 probe produced identical output
bytes on both backends. MSan saw the generic buffer as defined and the
first byte of the Haswell buffer as poisoned.

The x86_64 eta wrappers declassify the candidate buffer over `buflen` bytes
and reclassify `outlen * sizeof(int32_t)` output bytes. The assembly uses a
`vpmovmskb` acceptance mask to index its compaction table and `popcnt` to
advance the output position. Scalar branches check input and output bounds
and accept nibbles below 15 (eta 2) or 9 (eta 4). The eta-2 scalar tail
computes modulo 5 with fixed multiplications and shifts, and the eta-4 tail
subtracts the nibble from 4. The accepted coefficient goes to the next
output slot. For uniformly distributed nibbles, every accepted coefficient
has the same number of preimages, so the acceptance pattern leaves the
distribution of accepted values uniform.

## Reproduce

From the repository root, run a representative configuration on each base and
the candidate. The workflow uses the same script with the matrix flags.

```sh
cd tests/ct_tooling
./ct_test.sh memsan clang generic -O1 ML-DSA-44
./ct_test.sh valgrind-varlat gcc haswell -O2 ML-DSA-44
```

For an unsuppressed run, build and execute `test_sig` directly from the
repository root. Repeat with each variant and build configuration.

```sh
build_ct() {
  build_dir=$1
  shift
  cmake -S . -B "$build_dir" -GNinja -DCMAKE_BUILD_TYPE=Debug \
    -DOQS_DIST_BUILD=OFF -DOQS_USE_OPENSSL=OFF \
    -DOQS_MINIMAL_BUILD=SIG_ml_dsa_44 "$@" &&
    cmake --build "$build_dir" --target test_sig
}
build_ct build_ct_msan -DCMAKE_C_COMPILER=clang -DOQS_OPT_TARGET=generic \
  -DBUILD_SHARED_LIBS=ON -DOQS_ENABLE_TEST_CONSTANT_TIME_MEMSAN=ON \
  '-DCMAKE_C_FLAGS=-fsanitize=memory -fsanitize-recover=all -O1 -g' \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=memory \
  -DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=memory
MSAN_OPTIONS=halt_on_error=1:exit_code=123 build_ct_msan/tests/test_sig ML-DSA-44
build_ct build_ct_varlat -DCMAKE_C_COMPILER=gcc -DOQS_OPT_TARGET=haswell \
  -DCMAKE_C_FLAGS=-O2 -DOQS_ENABLE_TEST_CONSTANT_TIME_VALGRIND=ON
valgrind_varlat --tool=memcheck --variable-latency-errors=yes \
  --error-exitcode=123 --max-stackframe=20480000 --track-origins=yes \
  build_ct_varlat/tests/test_sig ML-DSA-44
```

The
[annotation regression](../test_mldsa_ct_annotations.c) checks the shadow of
a selected byte and its neighbors across classification, declassification
and reclassification. The workflow compiles it with `-fsanitize=memory`
against the ML-DSA-44 reference header at each MSan optimization setting.

## Coverage

The focused runs used Linux x86_64 under QEMU TCG, with GCC 15.2, Clang
21.1.8 and Valgrind-Varlat 3.26.0.GIT from the liboqs CI image. The ML-DSA-44
framework run on Clang 18.1.3 reported seven distinct MSan locations
on each base. The candidate passed the seven annotation settings and
all six focused generic MSan runs. All six variants passed the focused
unsuppressed Haswell Varlat run on both bases and the candidate.

| Check | Configurations and cases |
| --- | --- |
| Generic MSan | 42 runs across six variants and seven Clang settings. |
| Varlat | 192 runs across six variants, GCC and Clang, and `generic` and `haswell`. |
| Ordinary signatures and KATs | 12 signature and six KAT passes across the two backends. The six extmu KAT cases were skipped. |

Varlat uses eight settings per compiler (`-O0`, `-O1`, `-O2`, `-O3`,
`-Os`, `-Ofast`, and `-O2`/`-O3` with loop vectorization disabled).
Generic MSan uses the same Clang flags except `-O0`. Select
`memsan_generic_only` when dispatching the workflow for ML-DSA.

The CT harness exercises key generation and signing. Ordinary
`test_sig` builds also exercise verification and negative tests, and
`kat_sig` covers the standard variants. Dynamic results apply to the
executed paths and compiler configurations. The optimized SHAKE shadow
gap limits x86_64 MSan coverage, and AArch64 is outside this review.
