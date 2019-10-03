/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */
#include <kernel.h>
#include <errno.h>
#include <logging/log.h>
#include <sys/byteorder.h>
#include <sys/util.h>
#include <nfc/ndef/msg_parser.h>
#include <nfc/ndef/tnep_rec.h>
#include <nfc/tnep/poller.h>

LOG_MODULE_REGISTER(nfc_tnep_poller, CONFIG_NFC_TNEP_POLLER_LOG_LEVEL);

#define T4T_NDEF_NLEN_SIZE 2
#define T4T_NDEF_MIN_LEN 2
#define T2T_NDEF_MIN_LEN 0

/* Minimum len of service parameters records payload. According to
 * Tag NDEF Exchange Protocol 1.0 4.1.2.
 */
#define NFC_TNEP_SERVICE_PARAM_MIN_LEN 7

#define NFC_TNEP_STATUS_RECORD_LEN 1

#define NFC_TNEP_MIN_WAIT_TIME(_time) \
	(1 << (((ceiling_fraction((_time), 4)) - 1)))

enum tnep_poller_state {
	TNEP_POLLER_STATE_IDLE,
	TNEP_POLLER_STATE_SELECTING,
	TNEP_POLLER_STATE_DESELECTING,
	TNEP_POLLER_STATE_READING,
	TNEP_POLLER_STATE_UPDATING
};

struct tnep_poller {
	struct k_delayed_work tnep_work;
	struct nfc_tnep_buff rx;
	struct nfc_ndef_msg_desc *ndef_msg;
	const struct nfc_tnep_buff *tx;
	const struct nfc_ndef_tnep_rec_svc_param *svc_to_select;
	const struct nfc_ndef_tnep_rec_svc_param *active_svc;
	const struct nfc_tnep_poller_cb *cb;
	const struct nfc_tnep_poller_ndef_api *api;
	s64_t last_time;
	enum tnep_poller_state state;
	enum nfc_tnep_tag_type type;
	u32_t wait_time;
	u8_t retry_cnt;
};

static struct tnep_poller tnep;

static int tnep_tag_update_prepare(const struct nfc_ndef_msg_desc *msg,
				   size_t *tx_len)
{
	__ASSERT(tnep.api, "No provided TNEP NDEF Api");
	__ASSERT(tnep.api->ndef_read, "No provided API for reading NDEF");
	__ASSERT(tnep.api->ndef_update, "No provided API for writing NDEF");

	int err;
	size_t len = tnep.tx->size;
	u8_t *data = tnep.tx->data;

	*tx_len = 0;

	if (tnep.type == NFC_TNEP_TAG_TYPE_T4T) {
		data += T4T_NDEF_NLEN_SIZE;
		
		/* Subtract NDEF NLEN field, to calculate
		 * available space for NDEF Message correctly.
		 */
		len -= T4T_NDEF_NLEN_SIZE;
	}

	/* Encode NDEF Message into raw buffer. */
	err = nfc_ndef_msg_encode(msg,
				  data,
				  &len);
	if (err) {
		LOG_ERR("NDEF message encoding error");
		return err;
	}

	if (tnep.type == NFC_TNEP_TAG_TYPE_T4T) {
		sys_put_be16(len, tnep.tx->data);

		len += T4T_NDEF_NLEN_SIZE;
	}

	*tx_len = len;

	return 0;
}

bool status_record_check(const struct nfc_ndef_record_desc *record,
			 const u8_t *type_field,
			 size_t type_field_length)
{
	int cmp_res;

	if (!record->type_length) {
		return false;
	}

	cmp_res = memcmp(record->type,type_field, type_field_length);

	return (cmp_res == 0);
}

static int tnep_data_analyze(const struct nfc_ndef_msg_desc *msg,
			     struct nfc_tnep_poller_msg *poller_msg)
{
	/* Look for TNEP Status Record. */
	for (size_t i = 0; i < msg->record_count; i++) {
		const struct nfc_ndef_record_desc *record =
			msg->record[i];
		const struct nfc_ndef_bin_payload_desc *desc =
			(struct nfc_ndef_bin_payload_desc *)record->payload_descriptor;

		if (status_record_check(record, nfc_ndef_tnep_rec_type_status,
					NFC_NDEF_TNEP_REC_TYPE_LEN)) {
			if (desc->payload_length != NFC_TNEP_STATUS_RECORD_LEN) {
				LOG_ERR("Invalid TNEP status recoed length: %d. Length should be %d",
				desc->payload_length,
				NFC_TNEP_STATUS_RECORD_LEN);

				return -EINVAL;
			}

			poller_msg->msg = msg;
			poller_msg->status = desc->payload[0];

			return 0;
		}
	}

	LOG_DBG("Message does not have any status record");	

	return -EAGAIN;
}

static bool service_param_rec_check(u8_t tnf, const u8_t *type, u8_t length)
{
	__ASSERT_NO_MSG(type);

	int cmp;

	if (tnf != TNF_WELL_KNOWN) {
		return false;
	}

	if (length != NFC_NDEF_TNEP_REC_TYPE_LEN) {
		return false;
	}

	cmp = memcmp(type, nfc_ndef_tnep_rec_type_svc_param,
		     NFC_NDEF_TNEP_REC_TYPE_LEN);

	return (cmp == 0);
}

static int svc_param_record_get(const struct nfc_ndef_bin_payload_desc *bin_pay_desc,
				struct nfc_ndef_tnep_rec_svc_param *param)
{
	__ASSERT_NO_MSG(bin_pay_desc);
	__ASSERT_NO_MSG(param);

	const u8_t *nfc_data = bin_pay_desc->payload;

	/* Check length. */
	if (bin_pay_desc->payload_length < NFC_TNEP_SERVICE_PARAM_MIN_LEN) {
		LOG_ERR("To short Service Parameters Record.");
		return -EFAULT;
	}

	param->version = *(nfc_data++);
	param->uri_length = *(nfc_data++);

	/* Check the whole record length with Service Name URI. */
	if (bin_pay_desc->payload_length !=
	    (NFC_TNEP_SERVICE_PARAM_MIN_LEN + param->uri_length)) {
		LOG_ERR("Invalid Service Parameters Record length, expected %d, received: %d.",
			NFC_TNEP_SERVICE_PARAM_MIN_LEN + param->uri_length,
			bin_pay_desc->payload_length);
		return -EFAULT;
	}

	param->uri = nfc_data;
	
	nfc_data += param->uri_length;

	param->communication_mode = *(nfc_data++);
	param->min_time = *(nfc_data++);
	param->max_time_ext = *(nfc_data++);
	param->max_size = sys_get_be16(nfc_data);

	return 0;
}

static void on_svc_delayed_operation(void)
{
	int err;
	s64_t time_spent;

	time_spent = k_uptime_delta(&tnep.last_time);

	err = k_delayed_work_submit(&tnep.tnep_work,
				    time_spent > tnep.wait_time ?
				    K_NO_WAIT : (tnep.wait_time - time_spent));
	if (err && tnep.cb->error) {
		tnep.cb->error(err);
	}
}

static void tnep_delay_handler(struct k_work *work)
{
	int err = 0;

	switch (tnep.state) {
	case TNEP_POLLER_STATE_SELECTING:
	case TNEP_POLLER_STATE_READING:
	case TNEP_POLLER_STATE_UPDATING:
		err = tnep.api->ndef_read(tnep.rx.data, tnep.rx.size);

		break;
	
	default:
		err = -EPERM;

		break;

	}

	if (err) {
		if (tnep.cb->error) {
			tnep.cb->error(err);
		}
	}
}

int nfc_tnep_poller_init(struct nfc_ndef_msg_desc *ndef_msg,
			 const struct nfc_tnep_buff *tx_buff,
			 const struct nfc_tnep_poller_cb *cb)
{
	if (!ndef_msg || !tx_buff || !cb) {
		return -EINVAL;
	}

	if (!tx_buff->data) {
		LOG_ERR("NULL buffer provided");
		return -EINVAL;
	}

	if (tx_buff->size < 1) {
		LOG_ERR("Tx Buffer length cannot have length equal to 0");
		return -ENOMEM;
	}

	tnep.cb = cb;
	tnep.tx = tx_buff;
	tnep.ndef_msg = ndef_msg;

	k_delayed_work_init(&tnep.tnep_work, tnep_delay_handler);

	return 0;
}

int nfc_tnep_poller_api_set(const struct nfc_tnep_poller_ndef_api *api,
			    enum nfc_tnep_tag_type tag_type)
{
	if (!api) {
		return -EINVAL;
	}

	if ((tag_type != NFC_TNEP_TAG_TYPE_T2T) &&
	    (tag_type != NFC_TNEP_TAG_TYPE_T4T)) {
		LOG_ERR("Unknow NFC Tag Type");
		return -EINVAL;
	}

	tnep.api = api;
	tnep.type = tag_type;

	return 0;
}

int nfc_tnep_poller_svc_search(const struct nfc_ndef_msg_desc *ndef_msg,
			       struct nfc_ndef_tnep_rec_svc_param *params, u8_t *cnt)
{
	int err;
	const struct nfc_ndef_record_desc *record;
	u8_t size;

	if (!ndef_msg || !params || !cnt) {
		LOG_ERR("NULL argument.");
		return -EINVAL;
	}

	if ((*cnt) < 1) {
		return -ENOMEM;
	}

	size = *cnt;
	*cnt = 0;

	if (ndef_msg->record_count < 1) {
		LOG_DBG("No NDEF Records in message.");
		return 0;
	}

	/* Look for TNEP Service Parameters Records. */
	for (size_t i = 0; i < ndef_msg->record_count; i++) {
		record = ndef_msg->record[i];

		/* Check if record is Service Parameters Record. */ 
		if (service_param_rec_check(record->tnf, record->type,
					    record->type_length)) {
			/* Check if we have memory for service. */
			if ((*cnt) > size) {
				return -ENOMEM;
			}

			/* If service parameters record corrupted.
			 * Search for next valid record.
			 */
			err = svc_param_record_get(record->payload_descriptor,
						   &params[*cnt]);
			if (err) {
				LOG_DBG("Service Parameter Record corrupted."); 
				continue;
			}

			(*cnt)++;
		}
	}

	return 0;
}

int nfc_tnep_poller_svc_select(u8_t *svc_buff, size_t buff_len,
			       const struct nfc_ndef_tnep_rec_svc_param *svc,
			       u32_t max_ndef_area_size)
{
	int err;
	size_t len = 0;

	if (!svc || !svc_buff) {
		return -EINVAL;
	}

	if (buff_len < 1) {
		return -ENOMEM;
	}

	tnep.state = TNEP_POLLER_STATE_SELECTING;

	/* If service is currently selected do not select it twice. */
	if (svc == tnep.active_svc) {
		LOG_DBG("Service currently selected. Reading data");

		on_svc_delayed_operation();

		return 0;
	}

	if (svc->communication_mode != NFC_TNEP_COMM_MODE_SINGLE_RESPONSE) {
		LOG_DBG("Unsupported communication mode");
		return -ENOTSUP;
	}

	if (svc->max_size > max_ndef_area_size) {
		LOG_DBG("Service NDEF max size greater than max NDEF size. Service message size should be smaller than %d for this service",
			max_ndef_area_size);
		return -ENOMEM;
	}

	NFC_TNEP_SERIVCE_SELECT_RECORD_DESC_DEF(select_rec, svc->uri_length,
						svc->uri);

	/* Prepare the NDEF message and add the Service Select
	 * Record to it.
	 */
	nfc_ndef_msg_clear(tnep.ndef_msg);
	err = nfc_ndef_msg_record_add(tnep.ndef_msg,
				      &NFC_NDEF_TNEP_RECORD_DESC(select_rec));
	if (err) {
		LOG_ERR("Select record cannot be added to the NDEF message");
		return err;
	}

	err = tnep_tag_update_prepare(tnep.ndef_msg, &len);
	if (err) {
		LOG_ERR("TNEP NDEF Update preapre error");
		return err;
	}

	tnep.svc_to_select = svc;
	tnep.rx.data = svc_buff;
	tnep.rx.size = buff_len;

	/* Call API to send Update Request. */
	return tnep.api->ndef_update(tnep.tx->data, len);
}

int nfc_tnep_poller_svc_deselect(void)
{
	int err;
	size_t len = 0;

	/* Check if any service is selected. */
	if (!tnep.active_svc) {
		LOG_DBG("No service is active");
		return 0;
	}

	NFC_TNEP_SERIVCE_SELECT_RECORD_DESC_DEF(deselect_rec, 0,
						NULL);

	nfc_ndef_msg_clear(tnep.ndef_msg);
	err = nfc_ndef_msg_record_add(tnep.ndef_msg,
				      &NFC_NDEF_TNEP_RECORD_DESC(deselect_rec));
	if (err) {
		LOG_ERR("Service Deselect record cannot be added to the NDEF message");
		return err;
	}

	err = tnep_tag_update_prepare(tnep.ndef_msg, &len);
	if (err) {
		LOG_ERR("TNEP NDEF Update preapre error");
		return err;
	}


	tnep.state = TNEP_POLLER_STATE_DESELECTING;

	/* Call API to send Update Request. */
	return tnep.api->ndef_update(tnep.tx->data, len);
}

int nfc_tnep_poller_svc_read(u8_t *svc_buff, size_t buff_len)
{
	if (!svc_buff) {
		return -EINVAL;
	}

	if (buff_len < 1) {
		LOG_ERR("Buffer size smaller than 1");
		return -ENOMEM;
	}

	tnep.rx.data = svc_buff;
	tnep.rx.size = buff_len;
	tnep.state = TNEP_POLLER_STATE_READING;

	on_svc_delayed_operation();

	return 0;
}

int nfc_tnep_poller_svc_update(const struct nfc_ndef_msg_desc *msg,
			       u8_t *resp_data, size_t resp_buff_size)
{
	int err;
	size_t len = 0;

	if (!msg) {
		return -EINVAL;
	}

	if (msg->record_count < 1) {
		LOG_ERR("NDEF Message to Update without any NDEF Records");
		return -ENOMEM;
	}

	tnep.rx.data = resp_data;
	tnep.rx.size = resp_buff_size;

	tnep.state = TNEP_POLLER_STATE_UPDATING;

	err = tnep_tag_update_prepare(msg, &len);
	if (err) {
		LOG_ERR("TNEP NDEF Update preapre error");
		return err;
	}

	/* Call API to send Update Request. */
	return tnep.api->ndef_update(tnep.tx->data, len);
}

int nfc_tnep_poller_on_ndef_read(const u8_t *data, size_t len)
{
	int err;
	u8_t desc_buf[NFC_NDEF_PARSER_REQIRED_MEMO_SIZE_CALC(CONFIG_NFC_TNEP_POLLER_RX_MAX_RECORD_CNT)];
	size_t desc_buf_len = sizeof(desc_buf);
	struct nfc_tnep_poller_msg poller_msg; 
	struct nfc_ndef_msg_desc *msg;
	bool timeout = false;

	tnep.last_time = k_uptime_get();

	if (((tnep.type == NFC_TNEP_TAG_TYPE_T2T) &&
	    (len == T2T_NDEF_MIN_LEN)) ||
	    ((tnep.type == NFC_TNEP_TAG_TYPE_T4T) &&
	    (len == T4T_NDEF_MIN_LEN))) {
		if (tnep.retry_cnt <= tnep.active_svc->max_time_ext) {
			tnep.retry_cnt++;

		    	return tnep.api->ndef_read(tnep.rx.data,
						   tnep.rx.size);
		} else {
			timeout = true;
		}
	}

	tnep.retry_cnt = 0;
	memset(&poller_msg, 0, sizeof(poller_msg));

	err = nfc_ndef_msg_parse(desc_buf,
				 &desc_buf_len,
				 data, 
				 &len);
	if (err) {
		printk("Error during parsing a NDEF message, err: %d.\n", err);
		return err;
	}

	msg = (struct nfc_ndef_msg_desc *)desc_buf;

	err = tnep_data_analyze(msg, &poller_msg);
	if (err) {
		return err;
	}

	switch (tnep.state) {
	case TNEP_POLLER_STATE_SELECTING:
		if (tnep.cb->svc_selected) {
			tnep.cb->svc_selected(tnep.active_svc, &poller_msg,
					      timeout);
		}

		break;
	
	case TNEP_POLLER_STATE_READING:
		if (tnep.cb->svc_read) {
			tnep.cb->svc_read(tnep.active_svc, &poller_msg,
					  timeout);
		}

		break;

	case TNEP_POLLER_STATE_UPDATING:
		if (tnep.cb->svc_updated) {
			tnep.cb->svc_updated(tnep.active_svc, &poller_msg,
					     timeout);
		}

		break;

	default:

		return -ENOTSUP;
	}

	return 0;
}

int nfc_tnep_poller_on_ndef_update()
{
	tnep.last_time = k_uptime_get();

	switch (tnep.state) {
	case TNEP_POLLER_STATE_SELECTING:
		tnep.active_svc = tnep.svc_to_select;
		tnep.wait_time = NFC_TNEP_MIN_WAIT_TIME(tnep.active_svc->min_time);
		tnep.svc_to_select = NULL;
		
		/* Read service data. */
		on_svc_delayed_operation();

		break;

	case TNEP_POLLER_STATE_DESELECTING:
		tnep.active_svc = NULL;

		if (tnep.cb->svc_deselected) {
			tnep.cb->svc_deselected();
		}

		break;

	case TNEP_POLLER_STATE_UPDATING:
		/* Read service data after update. */
		on_svc_delayed_operation();

		break;
	
	default:
		return -ENOTSUP;
	}

	return 0;
}
