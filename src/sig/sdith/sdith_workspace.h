// SPDX-License-Identifier: MIT

#ifndef OQS_SDITH_WORKSPACE_H
#define OQS_SDITH_WORKSPACE_H

#include <oqs/common.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	// signing needs at most 64 bytes of randomness
	uint8_t entropy[64];
	uint8_t *temporary;
	size_t entropy_len;
	size_t temporary_len;
	bool cached;
} oqs_sdith_workspace;

OQS_STATUS oqs_sdith_workspace_acquire(oqs_sdith_workspace *workspace, uint64_t entropy_len, uint64_t temporary_len);
void oqs_sdith_workspace_release(oqs_sdith_workspace *workspace);
void oqs_sdith_workspace_destroy(void);

#endif
