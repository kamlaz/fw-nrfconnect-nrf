/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef NFC_TNEP_POLLER_
#define NFC_TNEP_POLLER_

/**@file
 *
 * @defgroup nfc_tnep_poller TNEP Protocol for NFC Poller
 * @{
 * @brief TNEP Protocol for NFC Poller API
 */

#ifdef _cplusplus
extern "C" {
#endif

#include <nfc/ndef/nfc_ndef_msg.h>
#include <nfc/ndef/tnep_rec.h>
#include <nfc/tnep/base.h>

enum nfc_tnep_tag_type {
	NFC_TNEP_TAG_TYPE_T2T,
	NFC_TNEP_TAG_TYPE_T4T
};

struct nfc_tnep_poller_msg {
	enum nfc_tnep_status_value status;
	const struct nfc_ndef_msg_desc *msg;
};

struct nfc_tnep_poller_ndef_api {
	int (*ndef_read)(u8_t *ndef_buff, u16_t ndef_len);

	int (*ndef_update)(u8_t *ndef_buff, u16_t ndef_len);
};

struct nfc_tnep_poller_cb {
	void (*svc_selected)(const struct nfc_ndef_tnep_rec_svc_param *param,
			     const struct nfc_tnep_poller_msg *msg,
			     bool timeout);

	void (*svc_deselected)(void);

	void (*svc_read)(const struct nfc_ndef_tnep_rec_svc_param *param,
			 const struct nfc_tnep_poller_msg *msg,
			 bool timeout);

	void (*svc_updated)(const struct nfc_ndef_tnep_rec_svc_param *param,
			    const struct nfc_tnep_poller_msg *rsp_msg,
			    bool timeout);

	void (*error)(int err);
};

struct nfc_tnep_buff {
	u8_t *data;
	size_t size;
};

int nfc_tnep_poller_init(struct nfc_ndef_msg_desc *ndef_msg,
			 const struct nfc_tnep_buff *tx_buff,
			 const struct nfc_tnep_poller_cb *cb);

int nfc_tnep_poller_api_set(const struct nfc_tnep_poller_ndef_api *api,
			    enum nfc_tnep_tag_type tag_type);

int nfc_tnep_poller_svc_search(const struct nfc_ndef_msg_desc *ndef_msg,
			       struct nfc_ndef_tnep_rec_svc_param *param, u8_t *cnt);

int nfc_tnep_poller_svc_select(u8_t *svc_buff, size_t buff_len,
			       const struct nfc_ndef_tnep_rec_svc_param *svc,
			       u32_t max_ndef_area_size);

int nfc_tnep_poller_svc_deselect(void);

int nfc_tnep_poller_svc_read(u8_t *svc_buff, size_t buff_len);

int nfc_tnep_poller_svc_update(const struct nfc_ndef_msg_desc *msg,
			       u8_t *resp_data, size_t resp_buff_size);

int nfc_tnep_poller_on_ndef_read(const u8_t *data, size_t len);

int nfc_tnep_poller_on_ndef_update();

const struct nfc_ndef_tnep_rec_svc_param *active_service_get(void);

#ifdef _cplusplus
}
#endif

/**
 * @}
 */

#endif /* NFC_TNEP_POLLER_ */
