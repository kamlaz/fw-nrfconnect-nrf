/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RPMSG_H
#define RPMSG_H

/**
 * @file
 * @defgroup rpmsg.h IPC communication lib
 * @{
 * @brief IPC communication lib.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**@brief Register receive callback.
 *
 * Callback is fired when data is received.
 *
 * @param[in] data Buffer containing received data.
 * @param[in] len Data length.
 */
void ipc_register_rx_callback(int (*rx_callback)(const u8_t *data, size_t len));

/**@brief Initialize IPM
 *
 * This function initialize IPM instance. It is important
 * to set desire target in the rpmsg.c file (REMOTE or MASTER).
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int ipc_init(void);

/**@brief Deinitialize IPM.
 *
 * This function deinitialize IPM instance.
 */
void ipc_deinit(void);

/**@brief Send data to the second core.
 *
 * This function sends data to the another core.
 *
 * @param[in] buff Data buffer.
 * @param[in] len Data length.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int ipc_transmit(const u8_t *buff, size_t buff_len);

#ifdef __cplusplus
}
#endif

/**
 *@}
 */

#endif /* RPMSG_H */
