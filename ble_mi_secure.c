/* Copyright (c) 2010-2017 Xiaomi. All Rights Reserved.
 *
 * The information contained herein is property of Xiaomi.
 * Terms and conditions of usage are described in detail in 
 * STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */
#include <stdarg.h>
#include "sdk_common.h"
#include "ble_mi_secure.h"
#include "ble_srv_common.h"

#define NRF_LOG_MODULE_NAME "ble_mi"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"

#define BLE_UUID_MI_AUTH   0x0010                      /**< The UUID of the AUTH   Characteristic. */
#define BLE_UUID_MI_BUFFER 0x0015                      /**< The UUID of the Buffer Characteristic. */
#define BLE_UUID_MI_PUBKEY 0x0016                      /**< The UUID of the PubKey Characteristic. */

#define PUBKEY_BYTE 255

static void auth_handler(uint8_t *pdata, uint8_t len);
void fast_xfer_rxd(fast_xfer_t *pxfer, uint8_t *pdata, uint8_t len);
void reliable_xfer_rxd(reliable_xfer_t *pxfer, uint8_t *pdata, uint8_t len);

static ble_mi_t   mi_srv;

fast_xfer_t m_app_pub = {.type = PUBKEY};
fast_xfer_t m_dev_pub = {.type = PUBKEY};

#define MAX_LOST_PKG_NUM  16
uint16_t  last_sn;
uint16_t  lost_sn[MAX_LOST_PKG_NUM];
uint8_t   lost_cnt;

uint16_t get_lost_sn(reliable_xfer_t *pxfer)
{
	uint16_t sn;
	uint8_t (*pdata)[18] = (void*)pxfer->pdata;
	int i;
	for(i = 0; i < MAX_LOST_PKG_NUM; i++) {
		if (lost_sn[i] != 0) {
			sn = lost_sn[i];
			lost_sn[i] = 0;
			lost_cnt--;
			break;
		}
	}
	
	if (i == MAX_LOST_PKG_NUM)
		return 0;
	else
		return sn;
}

int delete_lost_sn(uint16_t sn)
{
	int i;
	for(i = 0; i < MAX_LOST_PKG_NUM; i++) {
		if (lost_sn[i] == sn) {
			lost_sn[i] = 0;
			lost_cnt--;
			break;
		}
	}
	
	if (i == MAX_LOST_PKG_NUM)
		return 1;
	else
		return 0;
}

int add_lost_sn(uint16_t sn)
{
	int i;
	for(i = 0; i < MAX_LOST_PKG_NUM; i++) {
		if (lost_sn[i] == 0) {
			lost_sn[i] = sn;
			lost_cnt++;
			break;
		}
	}
	
	if (i == MAX_LOST_PKG_NUM)
		return 1;
	else
		return 0;
}

extern reliable_xfer_t m_cert;
/**@brief Function for handling the @ref BLE_GAP_EVT_CONNECTED event from the S110 SoftDevice.
 *
 * @param[in] p_mi_s    Xiaomi Service structure.
 * @param[in] p_ble_evt Pointer to the event received from BLE stack.
 */
static void on_connect(ble_evt_t * p_ble_evt)
{
    mi_srv.conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
}


/**@brief Function for handling the @ref BLE_GAP_EVT_DISCONNECTED event from the S110 SoftDevice.
 *
 * @param[in] p_mi_s    Xiaomi Service structure.
 * @param[in] p_ble_evt Pointer to the event received from BLE stack.
 */
static void on_disconnect(ble_evt_t * p_ble_evt)
{
    UNUSED_PARAMETER(p_ble_evt);
    mi_srv.conn_handle = BLE_CONN_HANDLE_INVALID;
}


/**@brief Function for handling the @ref BLE_GATTS_EVT_WRITE event from the S110 SoftDevice.
 *
 * @param[in] p_mi_s    Xiaomi Service structure.
 * @param[in] p_ble_evt Pointer to the event received from BLE stack.
 */
static void on_write(ble_evt_t * p_ble_evt)
{
    ble_gatts_evt_write_t * p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;
	
    if ((p_evt_write->len == 2) &&
		((p_evt_write->handle == mi_srv.auth_handles.cccd_handle)   ||
		 (p_evt_write->handle == mi_srv.pubkey_handles.cccd_handle) ||
		 (p_evt_write->handle == mi_srv.buffer_handles.cccd_handle)))
    {
        if (ble_srv_is_notification_enabled(p_evt_write->data))
        {
            mi_srv.is_notification_enabled = true;
        }
        else
        {
            mi_srv.is_notification_enabled = false;
        }
    }
    else if (p_evt_write->handle == mi_srv.auth_handles.value_handle)
    {
        auth_handler(p_evt_write->data, p_evt_write->len);
    }
    else if (p_evt_write->handle == mi_srv.buffer_handles.value_handle)
    {
		reliable_xfer_frame_t *pframe = (void*)p_evt_write->data;
		uint16_t  curr_sn = pframe->sn;
		if (curr_sn == 0 ) {
			if (pframe->f.ctrl.mode == 0) {
				fctrl_cmd_t cmd = pframe->f.ctrl.type;
				m_cert.type = cmd;
				switch (cmd) {
					case DEV_CERT:
						m_cert.amount = *(uint16_t*)pframe->f.ctrl.arg;
						last_sn       = 0;
						lost_cnt      = 0;
						memset(lost_sn, 0, MAX_LOST_PKG_NUM);
						break;
					default:
						NRF_LOG_ERROR("Unkown reliable CMD\n");
				}
			}
			else {
				fctrl_ack_t ack = pframe->f.ctrl.type;
				m_cert.type = ack;
				switch (ack) {
					case A_SUCCESS:
						break;
					case A_READY:
					case A_LOST:
					default:
						NRF_LOG_ERROR("Unkown reliable ACK\n");
				}
			}
		}
		else {
			uint16_t expect_sn = last_sn + 1;
			if (curr_sn == expect_sn) {
				last_sn = expect_sn;
			}
			else if(curr_sn < expect_sn) {
				delete_lost_sn(curr_sn);
			}
			else {
				while(curr_sn > expect_sn)
					add_lost_sn(expect_sn++);
				last_sn = expect_sn;
			}
			reliable_xfer_rxd(&m_cert, p_evt_write->data, p_evt_write->len);
			m_cert.curr_sn = curr_sn;
			if (curr_sn == m_cert.amount)
				m_cert.send_end = 1;
		}
    }
    else if (p_evt_write->handle == mi_srv.pubkey_handles.value_handle)
    {
		fast_xfer_frame_t *pframe = (void*)p_evt_write->data;
        if (pframe->type == PUBKEY && pframe->remain_len < PUBKEY_BYTE)
			fast_xfer_rxd(&m_app_pub, p_evt_write->data, p_evt_write->len);
		else
			NRF_LOG_ERROR("Unkown fast xfer data type\n");
    }
    else
    {
        // Do Nothing. This event is not relevant for this service.
    }
}



/**@brief Function for adding the Characteristic.
 *
 * @param[in]   uuid           UUID of characteristic to be added. (BASE is BLE_TYPE)
 * @param[in]   p_char_value   Point to the characteristic to be added. When it's NULL,
 *                             the value will be store in STACK RAM. Otherwise, it will
 *                             store in USER RAM. (MUST be GLOBAL in RAM)
 * @param[in]   char_len       Length of initial value. This will also be the maximum value.
 * @param[in]   char_props     GATT Characteristic Properties.
 * @param[out]  p_handles      Handles of new characteristic.
 *
 * @return      NRF_SUCCESS on success, otherwise an error code.
 */
static uint32_t char_add(uint16_t                        uuid,
                         uint8_t                        *p_char_value,
                         uint16_t                        char_len,
                         ble_gatt_char_props_t           char_props,
                         ble_gatts_char_handles_t       *p_handles)
{
    ble_uuid_t          ble_uuid;
    ble_gatts_char_md_t char_md;
    ble_gatts_attr_t    attr_char_value;
    ble_gatts_attr_md_t attr_md;
    ble_gatts_attr_md_t cccd_md;

    // The ble_gatts_attr_md_t structure uses bit fields. So we reset the memory to zero.
    memset(&char_md, 0, sizeof(char_md));

    char_md.char_props = char_props;

    if (char_props.notify) {
		memset(&cccd_md, 0, sizeof(cccd_md));
		cccd_md.vloc         = BLE_GATTS_VLOC_STACK;
		BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.read_perm);
		BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.write_perm);
        char_md.p_cccd_md    = &cccd_md;
    } else {
        char_md.p_cccd_md    = NULL;
    }

    memset(&attr_md, 0, sizeof(attr_md));

	BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
	BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);
    attr_md.vloc       = p_char_value == NULL ? BLE_GATTS_VLOC_STACK : BLE_GATTS_VLOC_USER;
    attr_md.rd_auth    = 0;
    attr_md.wr_auth    = 0;
    attr_md.vlen       = 1;

    BLE_UUID_BLE_ASSIGN(ble_uuid, uuid);

    memset(&attr_char_value, 0, sizeof(attr_char_value));

    attr_char_value.p_uuid    = &ble_uuid;
    attr_char_value.p_attr_md = &attr_md;
    attr_char_value.init_len  = 1;
    attr_char_value.init_offs = 0;
    attr_char_value.max_len   = char_len;
    attr_char_value.p_value   = p_char_value ? p_char_value : NULL;

    return sd_ble_gatts_characteristic_add(mi_srv.service_handle,
	                                       &char_md,
	                                       &attr_char_value,
	                                       p_handles);
}

static void auth_handler(uint8_t *pdata, uint8_t len)
{
	return;
}

void fast_xfer_rxd(fast_xfer_t *pxfer, uint8_t *pdata, uint8_t len)
{
	fast_xfer_frame_t *pframe = (fast_xfer_frame_t*)pdata;
	
	uint8_t          full_len = pxfer->full_len;
	uint8_t          curr_len = pframe->remain_len;
	uint8_t          data_len = len - 2;

	if ((pframe->remain_len < data_len)) {
		NRF_LOG_ERROR(" illegal frame parameter : len\n");
		pxfer->full_len = 0;
		return;
	}
	if (full_len < curr_len )
		pxfer->full_len = curr_len;

	uint8_t *addr = pxfer->data + sizeof(pxfer->data) - curr_len;
	memcpy(addr,
		   pframe->data,
		   data_len);

	pxfer->curr_len += data_len;
	
	if (pxfer->curr_len == pxfer->full_len ) {
		pxfer->avail = 1;
		pxfer->curr_len = 0;
	}
}

int fast_xfer_recive(fast_xfer_t *pxfer)
{
	if (!pxfer->avail) {
		return 1;
	}
	else {
		return 0;
	}
}

int fast_xfer_txd(fast_xfer_t *pxfer)
{
	ble_gatts_hvx_params_t hvx_params;
	fast_xfer_tx_frame_t        frame;
	uint32_t                 data_len;
	uint32_t                    errno;

    memset(&hvx_params, 0, sizeof(hvx_params));
	memset(&frame,      0, sizeof(frame));

	data_len = pxfer->curr_len > 18 ? 18 : pxfer->curr_len;

	frame.remain_len = pxfer->curr_len;
	frame.type       = pxfer->type;
	memcpy(frame.data,
	       pxfer->data + pxfer->full_len - pxfer->curr_len,
	       data_len);
	
	data_len += 2;      // add 2 bytes (remain_len and type)
    hvx_params.handle = mi_srv.pubkey_handles.value_handle;
    hvx_params.p_data = (void*)&frame;
    hvx_params.p_len  = (uint16_t*)&data_len;
    hvx_params.type   = BLE_GATT_HVX_NOTIFICATION;

    errno = sd_ble_gatts_hvx(mi_srv.conn_handle, &hvx_params);

	if (errno == NRF_SUCCESS) {
		pxfer->curr_len -= data_len - 2;
		NRF_LOG_INFO("Send %d bytes\n", data_len-2);
	}

	return errno;
}

int fast_xfer_send(fast_xfer_t *pxfer)
{
	uint32_t errno;
	if ((mi_srv.conn_handle == BLE_CONN_HANDLE_INVALID) || (!mi_srv.is_notification_enabled))
    {
        return NRF_ERROR_INVALID_STATE;
    }
	
	uint8_t free_packet_cnt;
	sd_ble_tx_packet_count_get(mi_srv.conn_handle, &free_packet_cnt);
	NRF_LOG_INFO("free TX packets: %d\n", free_packet_cnt);
	while( free_packet_cnt-- ) {
		errno = fast_xfer_txd(pxfer);
		if (errno != NRF_SUCCESS) {
			NRF_LOG_ERROR("Notify error %d", errno);
			break;
		}
		else if (pxfer->curr_len == 0 ) {
			NRF_LOG_INFO("TX completed.\n");
			return 0;
		}
	}
	return 1;
}

void reliable_xfer_rxd(reliable_xfer_t *pxfer, uint8_t *pdata, uint8_t len)
{
	reliable_xfer_frame_t      *pframe = (void*)pdata;
	uint8_t                   data_len = len - sizeof(pframe->sn);

	memcpy(pxfer->pdata + (pframe->sn - 1) * 18, pframe->f.data, data_len);
	
	pxfer->rxcnt ++;
	
	if (pxfer->rxcnt == pxfer->amount)
		pxfer->avail = 1;
}

int reliable_xfer_rx_ready()
{
	ble_gatts_hvx_params_t hvx_params;
	reliable_xfer_frame_t       frame;
	uint16_t                 data_len;
	uint32_t                    errno;

    memset(&hvx_params, 0, sizeof(hvx_params));
    memset(&frame,      0, sizeof(frame));

	frame.sn          =       0;
	frame.f.ctrl.type = A_READY; 
	data_len = sizeof(frame.sn) + sizeof(fctrl_ack_t);
    hvx_params.handle = mi_srv.buffer_handles.value_handle;
    hvx_params.p_data = (void*)&frame;
    hvx_params.p_len  = &data_len;
    hvx_params.type   = BLE_GATT_HVX_NOTIFICATION;

    errno = sd_ble_gatts_hvx(mi_srv.conn_handle, &hvx_params);

	if (errno != NRF_SUCCESS) {
		NRF_LOG_INFO("can't send ack\n");
	}

	return errno;
}

int reliable_xfer_cmd(fctrl_cmd_t cmd, ...)
{
	ble_gatts_hvx_params_t hvx_params;
	reliable_xfer_frame_t       frame;
	uint16_t                 data_len;
	uint32_t                    errno;
	uint16_t                      arg;

    memset(&hvx_params, 0, sizeof(hvx_params));
    memset(&frame,      0, sizeof(frame));
	
	frame.f.ctrl.type =     cmd;

	va_list ap;
	va_start(ap, cmd);
	arg = va_arg(ap, int);
	if ( arg != 0 ) {
		*(uint16_t*)frame.f.ctrl.arg = arg;
	}
	va_end(ap);

	data_len = sizeof(frame.sn) + sizeof(frame.f.ctrl);
    hvx_params.handle = mi_srv.buffer_handles.value_handle;
    hvx_params.p_data = (void*)&frame;
    hvx_params.p_len  = &data_len;
    hvx_params.type   = BLE_GATT_HVX_NOTIFICATION;

    errno = sd_ble_gatts_hvx(mi_srv.conn_handle, &hvx_params);

	if (errno != NRF_SUCCESS) {
		NRF_LOG_INFO("can't send ack\n");
	}

	return errno;
}

int reliable_xfer_ack(fctrl_ack_t ack, ...)
{
	ble_gatts_hvx_params_t hvx_params;
	reliable_xfer_frame_t       frame;
	uint16_t                 data_len;
	uint32_t                    errno;

    memset(&hvx_params, 0, sizeof(hvx_params));
    memset(&frame,      0, sizeof(frame));

	frame.f.ctrl.mode =     1;
	frame.f.ctrl.type =   ack;
	data_len = sizeof(frame.sn) + sizeof(frame.f.ctrl.type) + sizeof(frame.f.ctrl.mode);

	if (ack == A_LOST) {
		va_list ap;
		va_start(ap, ack);
		uint16_t arg = va_arg(ap, int);
		if ( arg != 0 ) {
			*(uint16_t*)frame.f.ctrl.arg = arg;
			data_len += sizeof(frame.f.ctrl.arg);
		}
		va_end(ap);
	}
	
    hvx_params.handle = mi_srv.buffer_handles.value_handle;
    hvx_params.p_data = (void*)&frame;
    hvx_params.p_len  = &data_len;
    hvx_params.type   = BLE_GATT_HVX_NOTIFICATION;

    errno = sd_ble_gatts_hvx(mi_srv.conn_handle, &hvx_params);

	if (errno != NRF_SUCCESS) {
		NRF_LOG_INFO("can't send ack\n");
	}

	return errno;
}

void ble_mi_on_ble_evt(ble_evt_t * p_ble_evt)
{
    if (p_ble_evt == NULL)
    {
        return;
    }

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            on_connect(p_ble_evt);
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            on_disconnect(p_ble_evt);
            break;

        case BLE_GATTS_EVT_WRITE:
            on_write(p_ble_evt);
            break;

		case BLE_EVT_TX_COMPLETE:
			break;

        default:
            // No implementation needed.
            break;
    }
}

uint32_t ble_mi_init(const ble_mi_init_t * p_mi_s_init)
{
    uint32_t      err_code;
    ble_uuid_t    ble_uuid;

//    VERIFY_PARAM_NOT_NULL(p_mi_s_init);

    // Initialize the service structure.
    mi_srv.conn_handle             = BLE_CONN_HANDLE_INVALID;
    mi_srv.data_handler            = p_mi_s_init->data_handler;
    mi_srv.is_notification_enabled = false;

    /**@snippet [Adding proprietary Service to S13x SoftDevice] */
    // Add a MI UUID.
	mi_srv.uuid_type = BLE_UUID_TYPE_BLE;
	BLE_UUID_BLE_ASSIGN(ble_uuid, BLE_UUID_MI_SERVICE);
    // Add the service.
    err_code = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY,
                                        &ble_uuid,
                                        &mi_srv.service_handle);
    VERIFY_SUCCESS(err_code);

    // Add the AUTH Characteristic.
	ble_gatt_char_props_t char_props = {0};
	char_props.write_wo_resp         = 1;
	char_props.notify                = 1;
    err_code = char_add(BLE_UUID_MI_AUTH, NULL, 4, char_props, &mi_srv.auth_handles);
    VERIFY_SUCCESS(err_code);

    // Add the Buffer Characteristic.
	char_props = (ble_gatt_char_props_t){0};
	char_props.write_wo_resp         = 1;
	char_props.notify                = 1;
    err_code = char_add(BLE_UUID_MI_BUFFER, NULL, 20, char_props, &mi_srv.buffer_handles);
    VERIFY_SUCCESS(err_code);

	// Add the Pubkey Characteristic.
	char_props = (ble_gatt_char_props_t){0};
	char_props.write_wo_resp         = 1;
	char_props.notify                = 1;
	err_code = char_add(BLE_UUID_MI_PUBKEY, NULL, 20, char_props, &mi_srv.pubkey_handles);
    VERIFY_SUCCESS(err_code);
    
	return NRF_SUCCESS;
}

uint32_t ble_mi_string_send(uint8_t * p_string, uint16_t length)
{
    ble_gatts_hvx_params_t hvx_params;


    if ((mi_srv.conn_handle == BLE_CONN_HANDLE_INVALID) || (!mi_srv.is_notification_enabled))
    {
        return NRF_ERROR_INVALID_STATE;
    }

    if (length > BLE_MI_MAX_DATA_LEN)
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    memset(&hvx_params, 0, sizeof(hvx_params));

    hvx_params.handle = mi_srv.buffer_handles.value_handle;
    hvx_params.p_data = p_string;
    hvx_params.p_len  = &length;
    hvx_params.type   = BLE_GATT_HVX_NOTIFICATION;

    return sd_ble_gatts_hvx(mi_srv.conn_handle, &hvx_params);
}

