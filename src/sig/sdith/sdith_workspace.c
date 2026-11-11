// SPDX-License-Identifier: MIT

#include "sdith_workspace.h"

#include <oqs/sig_sdith.h>
#include <stdatomic.h>

static uint8_t *cached_buffer = NULL;
static size_t cached_capacity = 0;
static atomic_flag cache_busy = ATOMIC_FLAG_INIT;
static atomic_bool reuse_enabled = ATOMIC_VAR_INIT(false);

OQS_API void OQS_SIG_sdith_set_workspace_reuse(bool enabled) {
	atomic_store_explicit(&reuse_enabled, enabled, memory_order_release);
}

OQS_STATUS oqs_sdith_workspace_acquire(oqs_sdith_workspace *workspace, uint64_t entropy_len, uint64_t temporary_len) {
	if (workspace == NULL || entropy_len > sizeof(workspace->entropy) || temporary_len > SIZE_MAX) {
		return OQS_ERROR;
	}
	*workspace = (oqs_sdith_workspace) {0};
	workspace->entropy_len = (size_t) entropy_len;
	workspace->temporary_len = (size_t) temporary_len;

	// concurrent calls allocate their own buffer
	if (atomic_load_explicit(&reuse_enabled, memory_order_acquire) &&
	        !atomic_flag_test_and_set_explicit(&cache_busy, memory_order_acquire)) {
		if (cached_capacity < workspace->temporary_len) {
			OQS_MEM_secure_free(cached_buffer, cached_capacity);
			cached_buffer = OQS_MEM_malloc(workspace->temporary_len);
			cached_capacity = cached_buffer != NULL ? workspace->temporary_len : 0;
		}
		workspace->temporary = cached_buffer;
		workspace->cached = true;
		if (workspace->temporary == NULL) {
			atomic_flag_clear_explicit(&cache_busy, memory_order_release);
			workspace->cached = false;
			return OQS_ERROR;
		}
	} else {
		workspace->temporary = OQS_MEM_malloc(workspace->temporary_len);
		if (workspace->temporary == NULL) {
			return OQS_ERROR;
		}
	}
	return OQS_SUCCESS;
}

void oqs_sdith_workspace_release(oqs_sdith_workspace *workspace) {
	if (workspace == NULL) {
		return;
	}
	OQS_MEM_cleanse(workspace->entropy, workspace->entropy_len);
	if (workspace->cached) {
		// memory outside this region was wiped by earlier calls
		OQS_MEM_cleanse(workspace->temporary, workspace->temporary_len);
		atomic_flag_clear_explicit(&cache_busy, memory_order_release);
	} else {
		OQS_MEM_secure_free(workspace->temporary, workspace->temporary_len);
	}
	*workspace = (oqs_sdith_workspace) {0};
}

void oqs_sdith_workspace_destroy(void) {
	// active calls must finish before OQS_destroy()
	atomic_store_explicit(&reuse_enabled, false, memory_order_release);
	OQS_MEM_secure_free(cached_buffer, cached_capacity);
	cached_buffer = NULL;
	cached_capacity = 0;
	atomic_flag_clear_explicit(&cache_busy, memory_order_release);
}
