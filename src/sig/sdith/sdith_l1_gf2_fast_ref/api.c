// The scheme's sizes, parameter set and symbol prefixes. liboqs copies
// this file next to the params.h of each scheme and implementation.
#include "params.h"

#include <stdlib.h>
#include <string.h>

#include <oqs/rand.h>
#include <oqs/common.h>

#include "sdith_signature.h"
#include "api.h"
#include "../sdith_workspace.h"


OQS_STATUS crypto_sign_keypair(uint8_t* public_key, uint8_t* secret_key) {
  if (CRYPTO_BYTES != sdith_signature_bytes(&SIGNATURE_PARAMS) ||
      CRYPTO_PUBLICKEYBYTES != sdith_public_key_bytes(&SIGNATURE_PARAMS) ||
      CRYPTO_SECRETKEYBYTES != sdith_secret_key_bytes(&SIGNATURE_PARAMS))
    return OQS_ERROR;
  uint64_t entropy_bytes = sdith_keygen_entropy_bytes(&SIGNATURE_PARAMS);
  uint64_t tmp_bytes = sdith_keygen_tmp_bytes(&SIGNATURE_PARAMS);
  uint8_t* entropy = OQS_MEM_malloc(entropy_bytes);
  if (entropy == NULL) return OQS_ERROR;
  uint8_t* tmp_space = OQS_MEM_malloc(tmp_bytes);
  if (tmp_space == NULL) {
    OQS_MEM_insecure_free(entropy);
    return OQS_ERROR;
  }

  OQS_randombytes(entropy, entropy_bytes);
  sdith_keygen(&SIGNATURE_PARAMS, secret_key, public_key, entropy, tmp_space);

  // tmp_space holds the raw solution, entropy holds the sk_seed: wipe both.
  OQS_MEM_secure_free(tmp_space, tmp_bytes);
  OQS_MEM_secure_free(entropy, entropy_bytes);
  return OQS_SUCCESS;
}

OQS_STATUS crypto_sign_sign(
	uint8_t *signature,
	size_t *signature_len,
	const uint8_t *message,
	size_t message_len,
	const uint8_t *secret_key)
{
  uint64_t entropy_bytes = sdith_signature_entropy_bytes(&SIGNATURE_PARAMS);
  uint64_t tmp_bytes = sdith_signature_tmp_bytes(&SIGNATURE_PARAMS);
  oqs_sdith_workspace workspace;
  if (oqs_sdith_workspace_acquire(&workspace, entropy_bytes, tmp_bytes) != OQS_SUCCESS) {
    return OQS_ERROR;
  }

  OQS_randombytes(workspace.entropy, workspace.entropy_len);
  sdith_sign(&SIGNATURE_PARAMS, signature, message, message_len, secret_key, workspace.entropy, workspace.temporary);
  *signature_len = CRYPTO_BYTES;

  oqs_sdith_workspace_release(&workspace);
  return OQS_SUCCESS;
}

OQS_STATUS crypto_sign_verify(
	const uint8_t *signature,
	size_t signature_len,
	const uint8_t *message,
	size_t message_len,
	const uint8_t *public_key
) {
  if (signature_len != CRYPTO_BYTES) return OQS_ERROR;
  uint64_t tmp_bytes = sdith_verify_tmp_bytes(&SIGNATURE_PARAMS);
  oqs_sdith_workspace workspace;
  if (oqs_sdith_workspace_acquire(&workspace, 0, tmp_bytes) != OQS_SUCCESS) return OQS_ERROR;

  uint8_t res = sdith_verify(
    &SIGNATURE_PARAMS,
    signature,
    message,
    message_len,
    public_key,
    workspace.temporary
  );

  oqs_sdith_workspace_release(&workspace);
  return res ? OQS_SUCCESS : OQS_ERROR;
}
