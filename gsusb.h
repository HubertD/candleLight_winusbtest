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

#ifdef __cplusplus
}
#endif
