#ifndef __MI_BEACON_H__
#define __MI_BEACON_H__
#include <stdint.h>

typedef enum {
	MI_EVT_BASE          = 0x0000,
	MI_EVT_CONNECT       = 0x0001,
	MI_EVT_SIMPLE_PAIR   = 0x0002,
	MI_EVT_LOCK          = 0x0005,

	MI_STA_BASE         = 0x1000,
	MI_STA_BUTTON       = 0x1001,
	MI_STA_SLEEP        = 0x1002,
	MI_STA_RSSI         = 0x1003,
	MI_STA_TEMPARATURE  = 0x1004,
	MI_STA_WATER_BOIL   = 0x1005,
	MI_STA_HUMIDITY     = 0x1006,
	MI_STA_LUMINA       = 0x1007,
	MI_STA_SOIL_PF      = 0x1008,
	MI_STA_SOIL_EC      = 0x1009,
	MI_STA_BATTERY      = 0x100A,

} mibeacon_obj_t;

typedef struct {
	uint8_t				factory_new  :1;
	uint8_t				is_connect   :1;
	uint8_t				is_central   :1;
	uint8_t				is_encrypt   :1;

	uint8_t				mac_include  :1;
	uint8_t				cap_include  :1;
	uint8_t				evt_include  :1;
	uint8_t				manu_data_include    :1;
	uint8_t				manu_title_include   :1;

	uint8_t				bind_confirm :1;
	uint8_t				reserved     :1;
	uint8_t				secure_login :1;
	uint8_t				version      :4;
} mibeacon_frame_ctrl_t;

typedef struct {
	mibeacon_obj_t    type;
	uint8_t  len;
	uint8_t  val[20];
} mibeacon_event_t;

typedef struct {
	uint8_t connectable : 1;
	uint8_t centralable : 1;
	uint8_t encryptable : 1;
	uint8_t bondAbility : 2;
	uint8_t reserved    : 3;
} mibeacon_capability_t;

typedef struct {
	uint8_t len;
	uint8_t val[20];
} mibeacon_manu_data_t;

typedef struct {
	mibeacon_frame_ctrl_t frame_ctrl;
	uint16_t              pid;
	uint8_t               frame_cnt_lsb;
	uint8_t               *p_mac;
	mibeacon_capability_t *p_capability;
	mibeacon_event_t      *p_event;
	mibeacon_manu_data_t  *p_manu_data;
	mibeacon_manu_data_t  *p_manu_title;
} mibeacon_config_t;


int mi_beacon_data_set(mibeacon_config_t const * const in, uint8_t *out, uint8_t *out_len);
int mibeacon_event_push(mibeacon_obj_t evt, uint8_t len, void *val);
int mibeacon_init(void);


#endif  /* __MI_BEACON_H__ */ 


