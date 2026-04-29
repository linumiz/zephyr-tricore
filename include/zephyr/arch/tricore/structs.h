/*
 * Copyright (c) 2026 Infineon Technologies AG
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_TRICORE_STRUCTS_H_
#define ZEPHYR_INCLUDE_TRICORE_STRUCTS_H_

/* Per CPU architecture specifics */
struct _cpu_arch {
	/** Thread to reclaim CSAs from during next context switch */
	struct k_thread *to_reclaim;
};

#endif /* ZEPHYR_INCLUDE_TRICORE_STRUCTS_H_ */
