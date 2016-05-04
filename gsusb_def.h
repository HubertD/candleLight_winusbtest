#pragma once

#include <stdint.h>

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define __packed __attribute__((packed))

#ifdef __cplusplus
extern "C" {
#endif

/* Device specific constants */
enum gs_usb_breq {
    GS_USB_BREQ_HOST_FORMAT = 0,
    GS_USB_BREQ_BITTIMING,
    GS_USB_BREQ_MODE,
    GS_USB_BREQ_BERR,
    GS_USB_BREQ_BT_CONST,
    GS_USB_BREQ_DEVICE_CONFIG,

    CANDLELIGHT_TIMESTAMP_GET = 0x40,
    CANDLELIGHT_TIMESTAMP_ENABLE = 0x41,
};

enum gs_can_mode {
    /* reset a channel. turns it off */
    GS_CAN_MODE_RESET = 0,
    /* starts a channel */
    GS_CAN_MODE_START
};

enum gs_can_state {
    GS_CAN_STATE_ERROR_ACTIVE = 0,
    GS_CAN_STATE_ERROR_WARNING,
    GS_CAN_STATE_ERROR_PASSIVE,
    GS_CAN_STATE_BUS_OFF,
    GS_CAN_STATE_STOPPED,
    GS_CAN_STATE_SLEEPING
};

/* data types passed between host and device */
struct gs_host_config {
    u32 byte_order;
} __packed;
/* All data exchanged between host and device is exchanged in host byte order,
 * thanks to the struct gs_host_config byte_order member, which is sent first
 * to indicate the desired byte order.
 */

struct gs_device_config {
    u8 reserved1;
    u8 reserved2;
    u8 reserved3;
    u8 icount;
    u32 sw_version;
    u32 hw_version;
} __packed;

#define GS_CAN_MODE_NORMAL               0
#define GS_CAN_MODE_LISTEN_ONLY          (1<<0)
#define GS_CAN_MODE_LOOP_BACK            (1<<1)
#define GS_CAN_MODE_TRIPLE_SAMPLE        (1<<2)
#define GS_CAN_MODE_ONE_SHOT             (1<<3)

struct gs_device_mode {
    u32 mode;
    u32 flags;
} __packed;

struct gs_device_state {
    u32 state;
    u32 rxerr;
    u32 txerr;
} __packed;

struct gs_device_bittiming {
    u32 prop_seg;
    u32 phase_seg1;
    u32 phase_seg2;
    u32 sjw;
    u32 brp;
} __packed;

#define GS_CAN_FEATURE_LISTEN_ONLY      (1<<0)
#define GS_CAN_FEATURE_LOOP_BACK        (1<<1)
#define GS_CAN_FEATURE_TRIPLE_SAMPLE    (1<<2)
#define GS_CAN_FEATURE_ONE_SHOT         (1<<3)

struct gs_device_bt_const {
    u32 feature;
    u32 fclk_can;
    u32 tseg1_min;
    u32 tseg1_max;
    u32 tseg2_min;
    u32 tseg2_max;
    u32 sjw_max;
    u32 brp_min;
    u32 brp_max;
    u32 brp_inc;
} __packed;

#define GS_CAN_FLAG_OVERFLOW 1

struct gs_host_frame {
    u32 echo_id;
    u32 can_id;

    u8 can_dlc;
    u8 channel;
    u8 flags;
    u8 reserved;

    u8 data[8];

    u32 timestamp_us;
} __packed;
/* The GS USB devices make use of the same flags and masks as in
 * linux/can.h and linux/can/error.h, and no additional mapping is necessary.
 */

/* Only send a max of GS_MAX_TX_URBS frames per channel at a time. */
#define GS_MAX_TX_URBS 10
/* Only launch a max of GS_MAX_RX_URBS usb requests at a time. */
#define GS_MAX_RX_URBS 30

#ifdef __cplusplus
}
#endif
