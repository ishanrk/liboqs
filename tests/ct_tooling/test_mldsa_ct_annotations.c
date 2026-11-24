// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdio.h>
#include <sanitizer/msan_interface.h>

#define MLD_CONFIG_CT_TESTING_ENABLED
#include "sys.h"

int main(void) {
	uint8_t buffer[3] = { 1, 2, 3 };
	MLD_CT_TESTING_SECRET(&buffer[1], 1);
	if (__msan_test_shadow(&buffer[1], 1) != 0) {
		puts("FAIL: classification did not poison the requested byte");
		return 1;
	}
	if (__msan_test_shadow(&buffer[0], 1) != -1 ||
	    __msan_test_shadow(&buffer[2], 1) != -1) {
		puts("FAIL: classification poisoned neighboring bytes");
		return 2;
	}
	__msan_poison(buffer, sizeof(buffer));
	MLD_CT_TESTING_DECLASSIFY(&buffer[1], 1);
	if (__msan_test_shadow(&buffer[1], 1) != -1) {
		puts("FAIL: declassification did not unpoison the requested byte");
		return 3;
	}
	if (__msan_test_shadow(&buffer[0], 1) != 0 ||
	    __msan_test_shadow(&buffer[2], 1) != 0) {
		puts("FAIL: declassification unpoisoned neighboring bytes");
		return 4;
	}
	MLD_CT_TESTING_SECRET(&buffer[1], 1);
	if (__msan_test_shadow(&buffer[1], 1) != 0) {
		puts("FAIL: reclassification did not restore secrecy");
		return 5;
	}
	puts("PASS: classification, declassification, bounds, and reclassification");
	return 0;
}
