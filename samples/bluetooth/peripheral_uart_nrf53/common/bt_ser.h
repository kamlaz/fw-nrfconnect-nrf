/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BT_SER_H_
#define BT_SER_H_

#include <bluetooth/addr.h>

/**
 * @file
 * @defgroup bt_ser BLE Nordic UART Service serialization
 * @{
 * @brief BLE Nordic UART(NUS) serialization API.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**@brief Callback structure used bt the BT NUS serialization. */
struct bt_nus_cb {
	/**@brief A new connection has been established on the remote processor.
	 *
	 * This callback inform the application processor about a new
	 * connection.
	 *
	 * @param[in] addr Address of connected device.
	 * @param[in] err HCI error. Zero for success, non-zero otherwise.
	 */
	void (*connected)(const bt_addr_le_t *addr, u8_t err);

	/**@brief A connection has been terminated on the remote processor.
	 *
	 * This callback notifies a application processor that a
	 * connection has been disconnected.
	 *
	 * @param[in] addr Address of connected device.
	 * @param[in] reason HCI reason for the disconnection.
	 */
	void (*disconnected)(const bt_addr_le_t *addr, u8_t reason);

	/**@brief A new NUS data has been received on the remote processor.
	 *
	 * This callback notifies a application processor that the Nordic\
	 * UART Service received new data.
	 *
	 * @param[in] addr Device address which sent data.
	 * @param[in] data Received NUS data.
	 * @param[in] lem data length.
	 */
	void (*bt_received)(const bt_addr_le_t *addr, const u8_t *data,
			    size_t len);
};

/**@brief Parse received data from other core. This function
 *        should be used on Application and Network core always
 *        when serialization data was received.
 *
 * @param[in] data Received serialization data.
 * @param[in] len Data length.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int bt_nus_rx_parse(const u8_t *data, size_t len);

#if defined(NRF5340_XXAA_APPLICATION)
/**@brief Register bluetooth application callback.
 *
 * @param[in] cb Callback structure
 */
void bt_nus_callback_register(const struct bt_nus_cb *cb);

/**@brief Initialize Bluetooth NUS service on the Network
 *        core.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int bt_nus_init(void);

/**@brief Send received Nordic UART Service (NUS) to
 *        application processor.
 *
 * This function sends data to remote proccessor and
 * wait a specific amount of time for remote function
 * call return value. On remonte proccessor function
 * for sending data over NUS is called.
 *
 * @param[in] data NUS data to send.
 * @param[in] length Data length.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int bt_nus_transmit(const u8_t *data, size_t length);
#endif /* defined(NRF5340_XXAA_APPLICATION) */

#if defined(NRF5340_XXAA_NETWORK)
/**@brief Notify application processor about a new connection.
 *
 * @param[in] addr Address of connected device.
 * @param[in] err HCI error. Zero for success, non-zero otherwise.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int bt_nus_connection_evt_send(const bt_addr_le_t *addr, u8_t error);

/**@brief Notify application processor that a connection
 *        has been disconnected.
 *
 * @param[in] addr Address of connected device.
 * @param[in] err HCI error. Zero for success, non-zero otherwise.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int bt_nus_disconnection_evt_send(const bt_addr_le_t *addr, u8_t reason);

/**@brief Notify application processor that the NUS service
 *        received new data.
 *
 * @param[in] addr Device address which sent data.
 * @param[in] data Received data.
 * @param[in] length Data length.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int bt_nus_received_evt_send(const bt_addr_le_t *addr,
			     const u8_t *data, size_t length);

#endif /* defined(NRF5340_XXAA_NETWORK) */

#ifdef __cplusplus
}
#endif

/**
 *@}
 */

#endif /* BT_SER_H_ */
