#include "gsusb.h"
#include <stdio.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>
#include <strsafe.h>
#include "ch_9.h"


static bool usb_control_msg(WINUSB_INTERFACE_HANDLE hnd, uint8_t request, uint8_t requesttype, uint16_t value, uint16_t index, void *data, uint16_t size);
static bool gsusb_prepare_read(struct gsusb_device *dev, unsigned urb_num);
static bool gsusb_set_host_format(struct gsusb_device *dev);
static bool gsusb_get_device_info(struct gsusb_device *dev, struct gs_device_config *dconf);
static bool gsusb_get_bittiming_const(struct gsusb_device *dev, uint16_t channel, struct gs_device_bt_const *data);
static bool gsusb_read_di(HDEVINFO hdi, SP_DEVICE_INTERFACE_DATA interfaceData, struct gsusb_device *dev);
static bool gsusb_open_device(struct gsusb_device *dev);
static bool gsusb_close_device(struct gsusb_device *dev);


static bool gsusb_open_device(struct gsusb_device *dev)
{
    bool result;
    USB_INTERFACE_DESCRIPTOR ifaceDescriptor;
    WINUSB_PIPE_INFORMATION pipeInfo;

    dev->deviceHandle = CreateFile(
        dev->path,
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_WRITE | FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (dev->deviceHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    result = WinUsb_Initialize(dev->deviceHandle, &dev->winUSBHandle);
    if (!result) {
        return false;
    }

    result = WinUsb_QueryInterfaceSettings(dev->winUSBHandle, 0, &ifaceDescriptor);
    if (!result) {
        return false;
    }

    dev->interfaceNumber = ifaceDescriptor.bInterfaceNumber;

    for (uint8_t i=0; i<ifaceDescriptor.bNumEndpoints; i++) {

        if (!WinUsb_QueryPipe(dev->winUSBHandle, 0, i, &pipeInfo)) {
            return false;
        }

        if (pipeInfo.PipeType == UsbdPipeTypeBulk && USB_ENDPOINT_DIRECTION_IN(pipeInfo.PipeId)) {

            dev->bulkInPipe = pipeInfo.PipeId;

        } else if(pipeInfo.PipeType == UsbdPipeTypeBulk && USB_ENDPOINT_DIRECTION_OUT(pipeInfo.PipeId)) {

            dev->bulkOutPipe = pipeInfo.PipeId;

        } else {

            return false;

        }

    }

    if (!gsusb_set_host_format(dev)) {
        printf("could not set host format.\n");
        return false;
    }

    if (!gsusb_get_device_info(dev, &dev->dconf)) {
        printf("could not read device info.\n");
        return false;
    }

    if (!gsusb_get_bittiming_const(dev, 0, &dev->bt_const)) {
        printf("could not read bit timing constraints from device.\n");
        return false;
    }

    return true;
}

static bool gsusb_close_device(struct gsusb_device *dev)
{
    WinUsb_Free(dev->winUSBHandle);
    dev->winUSBHandle = NULL;
    CloseHandle(dev->deviceHandle);
    dev->deviceHandle = NULL;
}

bool gsusb_open(struct gsusb_device *dev)
{
    if (gsusb_open_device(dev)) {
        memset(dev->rxurbs, 0, sizeof(dev->rxurbs));
        for (unsigned i=0; i<GS_MAX_RX_URBS; i++) {
            HANDLE ev = CreateEvent(NULL, true, false, NULL);
            dev->rxevents[i] = ev;
            dev->rxurbs[i].ovl.hEvent = ev;
            gsusb_prepare_read(dev, i);
        }
        return true;
    } else {
        return false;
    }
}

static bool usb_control_msg(WINUSB_INTERFACE_HANDLE hnd, uint8_t request, uint8_t requesttype, uint16_t value, uint16_t index, void *data, uint16_t size)
{
    WINUSB_SETUP_PACKET packet;
    memset(&packet, 0, sizeof(packet));

    packet.Request = request;
    packet.RequestType = requesttype;
    packet.Value = value;
    packet.Index = index;
    packet.Length = size;

    unsigned long bytes_sent = 0;
    return WinUsb_ControlTransfer(hnd, packet, (uint8_t*)data, size, &bytes_sent, 0);
}

static bool gsusb_set_host_format(struct gsusb_device *dev)
{
    struct gs_host_config hconf;
    hconf.byte_order = 0x0000beef;

    bool rc = usb_control_msg(
        dev->winUSBHandle,
        GS_USB_BREQ_HOST_FORMAT,
        USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
        1,
        dev->interfaceNumber,
        &hconf,
        sizeof(hconf)
    );

    return rc;
}

bool gsusb_set_device_mode(struct gsusb_device *dev, uint16_t channel, uint32_t mode, uint32_t flags)
{
    struct gs_device_mode dm;
    dm.mode = mode;
    dm.flags = flags;

    bool rc = usb_control_msg(
        dev->winUSBHandle,
        GS_USB_BREQ_MODE,
        USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
        channel,
        dev->interfaceNumber,
        &dm,
        sizeof(dm)
    );

    return rc;
}

bool gsusb_reset(struct gsusb_device *dev)
{
    return gsusb_set_device_mode(dev, 0, GS_CAN_MODE_RESET, 0);
}

static bool gsusb_get_device_info(struct gsusb_device *dev, struct gs_device_config *dconf)
{
    bool rc = usb_control_msg(
        dev->winUSBHandle,
        GS_USB_BREQ_DEVICE_CONFIG,
        USB_DIR_IN|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
        1,
        dev->interfaceNumber,
        dconf,
        sizeof(*dconf)
    );

    return rc;
}

static bool gsusb_get_bittiming_const(struct gsusb_device *dev, uint16_t channel, struct gs_device_bt_const *data)
{
    bool rc = usb_control_msg(
        dev->winUSBHandle,
        GS_USB_BREQ_BT_CONST,
        USB_DIR_IN|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
        channel,
        0,
        data,
        sizeof(*data)
    );

    return rc;
}

bool gsusb_set_bittiming(struct gsusb_device *dev, uint16_t channel, struct gs_device_bittiming *data)
{
    bool rc = usb_control_msg(
        dev->winUSBHandle,
        GS_USB_BREQ_BITTIMING,
        USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
        channel,
        0,
        data,
        sizeof(*data)
    );

    return rc;
}

bool gsusb_send_frame(struct gsusb_device *dev, uint16_t channel, struct gs_host_frame *frame)
{
    unsigned long bytes_sent = 0;

    frame->echo_id = 0;
    frame->channel = channel;

    bool rc = WinUsb_WritePipe(
        dev->winUSBHandle,
        dev->bulkOutPipe,
        (uint8_t*)frame,
        sizeof(*frame),
        &bytes_sent,
        0
    );

    return rc;
}

static bool gsusb_prepare_read(struct gsusb_device *dev, unsigned urb_num)
{
    bool rc =  WinUsb_ReadPipe(
        dev->winUSBHandle,
        dev->bulkInPipe,
        dev->rxurbs[urb_num].buf,
        sizeof(dev->rxurbs[urb_num].buf),
        NULL,
        &dev->rxurbs[urb_num].ovl
    );

    return rc;
}

bool gsusb_recv_frame(struct gsusb_device *dev, struct gs_host_frame *frame, uint32_t timeout_ms)
{
    bool retval = false;
    DWORD rv = WaitForMultipleObjects(GS_MAX_RX_URBS, dev->rxevents, false, timeout_ms);
    if ( (rv >= WAIT_OBJECT_0) && (rv < WAIT_OBJECT_0 + GS_MAX_RX_URBS) ) {
        DWORD urb_num = rv - WAIT_OBJECT_0;
        DWORD bytes_transfered;
        if (WinUsb_GetOverlappedResult(dev->winUSBHandle, &dev->rxurbs[urb_num].ovl, &bytes_transfered, false)) {
            if (bytes_transfered == sizeof(*frame)) {
                memcpy(frame, dev->rxurbs[urb_num].buf, sizeof(*frame));
            }
            gsusb_prepare_read(dev, urb_num);
            retval = true;
        }
    }
    return retval;
}

static bool gsusb_read_di(HDEVINFO hdi, SP_DEVICE_INTERFACE_DATA interfaceData, struct gsusb_device *dev)
{
    /* get required length first (this call always fails with an error) */
    ULONG requiredLength=0;
    SetupDiGetDeviceInterfaceDetail(hdi, &interfaceData, NULL, 0, &requiredLength, NULL);

    PSP_DEVICE_INTERFACE_DETAIL_DATA detail_data =
        (PSP_DEVICE_INTERFACE_DETAIL_DATA) LocalAlloc(LMEM_FIXED, requiredLength);

    if (detail_data != NULL) {
        detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
    } else {
        return false;
    }

    bool retval = false;
    ULONG length = requiredLength;
    if ( SetupDiGetDeviceInterfaceDetail(hdi, &interfaceData, detail_data, length, &requiredLength, NULL) ) {
        dev->state = gsusb_devstate_inuse;
        retval = !FAILED(StringCchCopy(dev->path, sizeof(dev->path), detail_data->DevicePath));
    }
    LocalFree(detail_data);

    if (!retval) {
        return false;
    }

    /* try to open to read device infos and see if it is avail */
    if (gsusb_open_device(dev)) {
        gsusb_close_device(dev);
        dev->state = gsusb_devstate_avail;
    } else {
        dev->state = gsusb_devstate_inuse;
    }

    return true;
}

bool gsusb_find_devices(struct gsusb_device *buf, size_t buf_size, uint16_t *num_devices)
{

    GUID guid;
    if (CLSIDFromString(L"{c15b4308-04d3-11e6-b3ea-6057189e6443}", &guid) != NOERROR) {
        return false;
    }

    HDEVINFO hdi = SetupDiGetClassDevs(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hdi == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool retval = false;
    *num_devices = 0;

    unsigned max_results = buf_size / sizeof(*buf);

    for (unsigned i=0; i<GSUSB_MAX_DEVICES; i++) {

        SP_DEVICE_INTERFACE_DATA interfaceData;
        interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        if (SetupDiEnumDeviceInterfaces(hdi, NULL, &guid, i, &interfaceData)) {

            if (i<max_results) {
                if (!gsusb_read_di(hdi, interfaceData, &buf[i])) {
                    break;
                }
            }

        } else {
            DWORD err = GetLastError();
            if (err==ERROR_NO_MORE_ITEMS) {
                *num_devices = i;
                retval = true;
            }
            break;
        }

    }

    SetupDiDestroyDeviceInfoList(hdi);

    return retval;
}

