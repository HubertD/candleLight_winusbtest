#pragma once

#include <stdbool.h>
#include "gsusb_def.h"
#include <windows.h>
#include <winbase.h>
#include <winusb.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GSUSB_MAX_DEVICES 256

typedef enum {
    GSUSB_ERR_OK                  =   0,
    GSUSB_ERR_CREATE_FILE         =  -1,
    GSUSB_ERR_WINUSB_INITIALIZE   =  -2,
    GSUSB_ERR_QUERY_INTERFACE     =  -3,
    GSUSB_ERR_QUERY_PIPE          =  -4,
    GSUSB_ERR_PARSE_IF_DESCR      =  -5,
    GSUSB_ERR_SET_HOST_FORMAT     =  -6,
    GSUSB_ERR_GET_DEVICE_INFO     =  -7,
    GSUSB_ERR_GET_BITTIMING_CONST =  -8,
    GSUSB_ERR_PREPARE_READ        =  -9,
    GSUSB_ERR_SET_DEVICE_MODE     = -10,
    GSUSB_ERR_SET_BITTIMING       = -11,
    GSUSB_ERR_BITRATE_FCLK        = -12,
    GSUSB_ERR_BITRATE_UNSUPPORTED = -13,
    GSUSB_ERR_SEND_FRAME          = -14,
    GSUSB_ERR_READ_TIMEOUT        = -15,
    GSUSB_ERR_READ_WAIT           = -16,
    GSUSB_ERR_READ_RESULT         = -17,
    GSUSB_ERR_READ_SIZE           = -18,
    GSUSB_ERR_SETUPDI_IF_DETAILS  = -19,
    GSUSB_ERR_SETUPDI_IF_DETAILS2 = -20,
    GSUSB_ERR_MALLOC              = -21,
    GSUSB_ERR_PATH_LEN            = -22,
    GSUSB_ERR_CLSID               = -23,
    GSUSB_ERR_GET_DEVICES         = -24,
    GSUSB_ERR_SETUPDI_IF_ENUM     = -25,
    GSUSB_ERR_SET_TIMESTAMP_MODE  = -26
} gsusb_err_t;

typedef enum {
    gsusb_devstate_avail,
    gsusb_devstate_inuse
} gsusb_devstate_t;

struct rx_urb {
    OVERLAPPED ovl;
    uint8_t buf[64];
};

struct gsusb_device {
    wchar_t path[256];
    gsusb_devstate_t state;
    gsusb_err_t last_error;

    HANDLE deviceHandle;
    WINUSB_INTERFACE_HANDLE winUSBHandle;
    UCHAR interfaceNumber;
    UCHAR bulkInPipe;
    UCHAR bulkOutPipe;

    struct gs_device_config dconf;
    struct gs_device_bt_const bt_const;
    struct rx_urb rxurbs[GS_MAX_RX_URBS];
    HANDLE rxevents[GS_MAX_RX_URBS];

};

bool gsusb_find_devices(struct gsusb_device *buf, size_t buf_size, uint16_t *num_devices);
bool gsusb_open(struct gsusb_device *dev);
bool gsusb_set_device_mode(struct gsusb_device *dev, uint16_t channel, uint32_t mode, uint32_t flags);
bool gsusb_reset(struct gsusb_device *dev);
bool gsusb_set_bitrate(struct gsusb_device *dev, uint16_t channel, uint32_t bitrate);
bool gsusb_set_bittiming(struct gsusb_device *dev, uint16_t channel, struct gs_device_bittiming *data);
bool gsusb_send_frame(struct gsusb_device *dev, uint16_t channel, struct gs_host_frame *frame);
bool gsusb_recv_frame(struct gsusb_device *dev, struct gs_host_frame *frame, uint32_t timeout_ms);
gsusb_err_t gsusb_last_error(struct gsusb_device *dev);

#ifdef __cplusplus
}
#endif
