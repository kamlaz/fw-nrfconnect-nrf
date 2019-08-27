/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <string.h>
#include <nfc/t4t/cc_file.h>

#define CC_LEN_MIN_VALUE 0x000F
#define CC_LEN_MAX_VALUE 0xFFFE

/* Major version number allowing first TLV block to be
 * Extended NDEF File Control TLV.
 */
#define NFC_T4T_EXTENDED_MAJOR_VER 0x03

/* Major version number allowing first TLV block to be NDEF File Control TLV */
#define NFC_T4T_REGULAR_MAJOR_VER 0x02

#define MLE_LEN_MIN_VALUE 0x000F
#define MLE_LEN_MAX_VALUE 0xFFFF

#define MLC_LEN_MIN_VALUE 0x0001
#define MLC_LEN_MAX_VALUE 0xFFFF

#define CC_LEN_FIELD_SIZE 2U
#define MAP_VER_FIELD_SIZE 1U
#define MLE_FIELD_SIZE 2U
#define MLC_FIELD_SIZE 2U

/* Gets least significant nibble (a 4-bit value) from a byte. */
#define LSN_GET(_val) ((_val) & 0x0F)

/* Gets most significant nibble (a 4-bit value) from a byte.i */
#define MSN_GET(val) ((val >> 4) & 0x0F)

/** @brief Function for validating arguments used by CC file parsing procedure.
 */
static int nfc_t4t_cc_args_validate(struct nfc_t4t_cc_file *t4t_cc_file,
				    u8_t *raw_data,
				    u16_t len)
{
	if ((!t4t_cc_file) ||
	    (!t4t_cc_file->tlv_block_array) ||
	    (!raw_data)) {
		return -EFAULT;
	}

	if ((len < CC_LEN_MIN_VALUE) || (len > CC_LEN_MAX_VALUE)) {
		return -EINVAL;
	}

	if (t4t_cc_file->max_tlv_blocks == 0) {
		return -ENOMEM;
	}

	return 0;
}


/** @brief Function for validating CC file descriptor content.
 */
int nfc_t4t_cc_file_validate(struct nfc_t4t_cc_file *t4t_cc_file)
{
	u16_t type = t4t_cc_file->tlv_block_array[0].type;

	if ((t4t_cc_file->major_version == NFC_T4T_EXTENDED_MAJOR_VER &&
	     type == EXTENDED_NDEF_FILE_CONTROL_TLV) ||
	     (t4t_cc_file->major_version == NFC_T4T_REGULAR_MAJOR_VER &&
	      type == NDEF_FILE_CONTROL_TLV)) {
		return 0;
	}

	return -EINVAL;
}


/** @brief Function for clearing all TLV blocks from CC file descriptor.
 */
static void nfc_t4t_cc_file_clear(struct nfc_t4t_cc_file *t4t_cc_file)
{
	t4t_cc_file->tlv_count = 0;
}


/** @brief Function for adding a TLV block to the CC file descriptor.
 */
static int nfc_t4t_tlv_block_insert(struct nfc_t4t_cc_file *t4t_cc_file,
                                    struct nfc_t4t_tlv_block *tlv_block)
{
	if (t4t_cc_file->tlv_count == t4t_cc_file->max_tlv_blocks) {
		return -ENOMEM;
	}

	/* Copy contents of the source block. */
	t4t_cc_file->tlv_block_array[t4t_cc_file->tlv_count] = *tlv_block;
	t4t_cc_file->tlv_count++;

	return 0;
}


int nfc_t4t_cc_file_parse(struct nfc_t4t_cc_file *t4t_cc_file,
			  u8_t *raw_data,
			  u16_t len)
{
	int err;

	err = nfc_t4t_cc_args_validate(t4t_cc_file, raw_data, len);
	if (err) {
		return err;
	}

	u8_t *offset = raw_data;
	nfc_t4t_cc_file_clear(t4t_cc_file);

	t4t_cc_file->len = sys_get_be16(offset);
	offset += CC_LEN_FIELD_SIZE;

	t4t_cc_file->major_version = MSN_GET(*offset);
	t4t_cc_file->minor_version = LSN_GET(*offset);
	offset += MAP_VER_FIELD_SIZE;

	t4t_cc_file->max_rapdu_size = sys_get_be16(offset);
	offset += MLE_FIELD_SIZE;

	t4t_cc_file->max_capdu_size = sys_get_be16(offset);
	offset += MLC_FIELD_SIZE;

	struct nfc_t4t_tlv_block new_block;
	len -= (offset - raw_data);

	while (len > 0) {
		u16_t tlv_len = len;

		err = nfc_t4t_file_control_tlv_parse(&new_block, offset, &tlv_len);
        	if (err) {
			return err;
		}

		offset += tlv_len;
		len -= tlv_len;

		err = nfc_t4t_tlv_block_insert(t4t_cc_file, &new_block);
		if (err) {
			return err;
		}
	}

	return nfc_t4t_cc_file_validate(t4t_cc_file);
}


struct nfc_t4t_tlv_block * nfc_t4t_file_content_get(struct nfc_t4t_cc_file *t4t_cc_file,
						    u16_t file_id)
{
	struct nfc_t4t_tlv_block *tlv_array = t4t_cc_file->tlv_block_array;
    
	for (size_t i = 0; i < t4t_cc_file->tlv_count; i++) {
		struct nfc_t4t_tlv_block_file_control_val *tlv_value =
			&tlv_array[i].value;

		if (tlv_value->file_id == file_id) {
			return (tlv_array + i);
		}
	}

	return NULL;
}


int nfc_t4t_file_content_set(struct nfc_t4t_cc_file *t4t_cc_file,
			     struct nfc_t4ti_tlv_block_file file,
			     u16_t file_id)
{
	struct nfc_t4t_tlv_block *tlv_block;

	tlv_block = nfc_t4t_file_content_get(t4t_cc_file, file_id);
	if (tlv_block) {
		tlv_block->value.file = file;

		return 0;
	}

	return -EACCES;
}


void nfc_t4t_cc_file_printout(struct nfc_t4t_cc_file *t4t_cc_file)
{
	LOG_INF("Capability Container File content: ")
	LOG_INF("CCLEN: %d ", t4t_cc_file->len);
	LOG_INF("Mapping Version: %d.%d ",
		t4t_cc_file->major_version,
		t4t_cc_file->minor_version);
	LOG_INF("MLe: %d ", t4t_cc_file->max_rapdu_size)
	LOG_INF("MLc: %d ", t4t_cc_file->max_capdu_size)

	LOG_INF("Capability Container File contains %d File Control TLV block(s).",
		t4t_cc_file->tlv_count);
	for (size_t i = 0; i < t4t_cc_file->tlv_count; i++) {
		nfc_t4t_file_control_tlv_printout(i, &t4t_cc_file->tlv_block_array[i]);
	}
}
