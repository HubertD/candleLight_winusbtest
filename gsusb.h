#pragma once

#include <stdbool.h>
#include "gsusb_def.h"
#include <windows.h>
#include <winbase.h>
#include <winusb.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rx_urb {
    OVERLAPPED ovl;
    uint8_t buf[64];
};

struct gsusb_device {
    WINUSB_INTERFACE_HANDLE winUSBHandle;
    UCHAR interfaceNumber;
    UCHAR bulkInPipe;
    UCHAR bulkOutPipe;

    struct gs_device_config dconf;
    struct gs_device_bt_const bt_const;
    struct rx_urb rxurbs[GS_MAX_RX_URBS];
    HANDLE rxevents[GS_MAX_RX_URBS];

};

bool gsusb_get_device_path(wchar_t *path, size_t len);
bool gsusb_open(struct gsusb_device *dev, wchar_t *path);
bool gsusb_set_device_mode(struct gsusb_device *dev, uint16_t channel, uint32_t mode, uint32_t flags);
bool gsusb_reset(struct gsusb_device *dev);
bool gsusb_set_bittiming(struct gsusb_device *dev, uint16_t channel, struct gs_device_bittiming *data);
bool gsusb_send_frame(struct gsusb_device *dev, uint16_t channel, struct gs_host_frame *frame);
bool gsusb_recv_frame(struct gsusb_device *dev, struct gs_host_frame *frame, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
