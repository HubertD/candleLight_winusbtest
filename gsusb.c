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
static bool gsusb_set_timestamp_mode(struct gsusb_device *dev, bool enable_timestamps);
static bool gsusb_get_device_info(struct gsusb_device *dev, struct gs_device_config *dconf);
static bool gsusb_get_bittiming_const(struct gsusb_device *dev, uint16_t channel, struct gs_device_bt_const *data);
static bool gsusb_read_di(HDEVINFO hdi, SP_DEVICE_INTERFACE_DATA interfaceData, struct gsusb_device *dev);
static bool gsusb_open_device(struct gsusb_device *dev);
static bool gsusb_close_device(struct gsusb_device *dev);

static bool gsusb_open_device(struct gsusb_device *dev)
{
    memset(dev->rxevents, 0, sizeof(dev->rxevents));
    memset(dev->rxurbs, 0, sizeof(dev->rxurbs));

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
        dev->last_error = GSUSB_ERR_CREATE_FILE;
        return false;
    }

    if (!WinUsb_Initialize(dev->deviceHandle, &dev->winUSBHandle)) {
        dev->last_error = GSUSB_ERR_WINUSB_INITIALIZE;
        goto close_handle;
    }

    USB_INTERFACE_DESCRIPTOR ifaceDescriptor;
    if (!WinUsb_QueryInterfaceSettings(dev->winUSBHandle, 0, &ifaceDescriptor)) {
        dev->last_error = GSUSB_ERR_QUERY_INTERFACE;
        goto winusb_free;
    }

    dev->interfaceNumber = ifaceDescriptor.bInterfaceNumber;
    unsigned pipes_found = 0;

    for (uint8_t i=0; i<ifaceDescriptor.bNumEndpoints; i++) {

        WINUSB_PIPE_INFORMATION pipeInfo;
        if (!WinUsb_QueryPipe(dev->winUSBHandle, 0, i, &pipeInfo)) {
            dev->last_error = GSUSB_ERR_QUERY_PIPE;
            goto winusb_free;
        }

        if (pipeInfo.PipeType == UsbdPipeTypeBulk && USB_ENDPOINT_DIRECTION_IN(pipeInfo.PipeId)) {
            dev->bulkInPipe = pipeInfo.PipeId;
            pipes_found++;
        } else if (pipeInfo.PipeType == UsbdPipeTypeBulk && USB_ENDPOINT_DIRECTION_OUT(pipeInfo.PipeId)) {
            dev->bulkOutPipe = pipeInfo.PipeId;
            pipes_found++;
        } else {
            dev->last_error = GSUSB_ERR_PARSE_IF_DESCR;
            goto winusb_free;
        }

    }

    if (pipes_found != 2) {
        dev->last_error = GSUSB_ERR_PARSE_IF_DESCR;
        goto winusb_free;
    }

    if (!gsusb_set_host_format(dev)) {
        dev->last_error = GSUSB_ERR_SET_HOST_FORMAT;
        goto winusb_free;
    }

    if (!gsusb_set_timestamp_mode(dev, true)) {
        dev->last_error = GSUSB_ERR_SET_TIMESTAMP_MODE;
        goto winusb_free;
    }

    if (!gsusb_get_device_info(dev, &dev->dconf)) {
        dev->last_error = GSUSB_ERR_GET_DEVICE_INFO;
        goto winusb_free;
    }

    if (!gsusb_get_bittiming_const(dev, 0, &dev->bt_const)) {
        dev->last_error = GSUSB_ERR_GET_BITTIMING_CONST;
        goto winusb_free;
    }

    dev->last_error = GSUSB_ERR_OK;
    return true;

winusb_free:
    WinUsb_Free(dev->winUSBHandle);

close_handle:
    CloseHandle(dev->deviceHandle);
    return false;
}

static bool gsusb_close_rxurbs(struct gsusb_device *dev)
{
    for (unsigned i=0; i<GS_MAX_RX_URBS; i++) {
        if (dev->rxevents[i] != NULL) {
            CloseHandle(dev->rxevents[i]);
        }
    }
    return true;
}

static bool gsusb_close_device(struct gsusb_device *dev)
{
    gsusb_close_rxurbs(dev);

    WinUsb_Free(dev->winUSBHandle);
    dev->winUSBHandle = NULL;
    CloseHandle(dev->deviceHandle);
    dev->deviceHandle = NULL;

    dev->last_error = GSUSB_ERR_OK;
    return true;
}

bool gsusb_open(struct gsusb_device *dev)
{
    if (gsusb_open_device(dev)) {
        for (unsigned i=0; i<GS_MAX_RX_URBS; i++) {
            HANDLE ev = CreateEvent(NULL, true, false, NULL);
            dev->rxevents[i] = ev;
            dev->rxurbs[i].ovl.hEvent = ev;
            if (!gsusb_prepare_read(dev, i)) {
                gsusb_close_rxurbs(dev);
                return false; // keep last_error from prepare_read call
            }
        }
        dev->last_error = GSUSB_ERR_OK;
        return true;
    } else {
        return false; // keep last_error from open_device call
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

    dev->last_error = rc ? GSUSB_ERR_OK : GSUSB_ERR_SET_HOST_FORMAT;
    return rc;
}

static bool gsusb_set_timestamp_mode(struct gsusb_device *dev, bool enable_timestamps)
{
    uint32_t ts_config = enable_timestamps ? 1 : 0;

    bool rc = usb_control_msg(
        dev->winUSBHandle,
        CANDLELIGHT_TIMESTAMP_ENABLE,
        USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
        1,
        ts_config,
        NULL,
        0
    );

    dev->last_error = rc ? GSUSB_ERR_OK : GSUSB_ERR_SET_TIMESTAMP_MODE;
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

    dev->last_error = rc ? GSUSB_ERR_OK : GSUSB_ERR_SET_DEVICE_MODE;
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

    dev->last_error = rc ? GSUSB_ERR_OK : GSUSB_ERR_GET_DEVICE_INFO;
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

    dev->last_error = rc ? GSUSB_ERR_OK : GSUSB_ERR_GET_BITTIMING_CONST;
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

    dev->last_error = rc ? GSUSB_ERR_OK : GSUSB_ERR_SET_BITTIMING;
    return rc;
}

bool gsusb_set_bitrate(struct gsusb_device *dev, uint16_t channel, uint32_t bitrate)
{

    if (dev->bt_const.fclk_can != 48000000) {
        /* this function only works for the candleLight base clock of 48MHz */
        dev->last_error = GSUSB_ERR_BITRATE_FCLK;
        return false;
    }

    struct gs_device_bittiming t;
    t.prop_seg = 1;
    t.sjw = 1;
    t.phase_seg1 = 13 - t.prop_seg;
    t.phase_seg2 = 2;

    switch (bitrate) {
        case 10000:
            t.brp = 300;
            break;

        case 20000:
            t.brp = 150;
            break;

        case 50000:
            t.brp = 60;
            break;

        case 83333:
            t.brp = 36;
            break;

        case 100000:
            t.brp = 30;
            break;

        case 125000:
            t.brp = 24;
            break;

        case 250000:
            t.brp = 12;
            break;

        case 500000:
            t.brp = 6;
            break;

        case 800000:
            t.brp = 4;
            t.phase_seg1 = 12 - t.prop_seg;
            t.phase_seg2 = 2;
            break;

        case 1000000:
            t.brp = 3;
            break;

        default:
            dev->last_error = GSUSB_ERR_BITRATE_UNSUPPORTED;
            return false;
    }

    return gsusb_set_bittiming(dev, channel, &t);
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

    dev->last_error = rc ? GSUSB_ERR_OK : GSUSB_ERR_SEND_FRAME;
    return rc;
}

static bool gsusb_prepare_read(struct gsusb_device *dev, unsigned urb_num)
{
    bool rc = WinUsb_ReadPipe(
        dev->winUSBHandle,
        dev->bulkInPipe,
        dev->rxurbs[urb_num].buf,
        sizeof(dev->rxurbs[urb_num].buf),
        NULL,
        &dev->rxurbs[urb_num].ovl
    );

    if (rc || (GetLastError()!=ERROR_IO_PENDING)) {
        dev->last_error = GSUSB_ERR_PREPARE_READ;
        return false;
    } else {
        dev->last_error = GSUSB_ERR_OK;
        return true;
    }
}

bool gsusb_recv_frame(struct gsusb_device *dev, struct gs_host_frame *frame, uint32_t timeout_ms)
{
    DWORD wait_result = WaitForMultipleObjects(GS_MAX_RX_URBS, dev->rxevents, false, timeout_ms);
    if (wait_result == WAIT_TIMEOUT) {
        dev->last_error = GSUSB_ERR_READ_TIMEOUT;
        return false;
    }

    if ( (wait_result < WAIT_OBJECT_0) || (wait_result >= WAIT_OBJECT_0 + GS_MAX_RX_URBS) ) {
        dev->last_error = GSUSB_ERR_READ_WAIT;
        return false;
    }

    DWORD urb_num = wait_result - WAIT_OBJECT_0;
    DWORD bytes_transfered;

    if (!WinUsb_GetOverlappedResult(dev->winUSBHandle, &dev->rxurbs[urb_num].ovl, &bytes_transfered, false)) {
        gsusb_prepare_read(dev, urb_num);
        dev->last_error = GSUSB_ERR_READ_RESULT;
        return false;
    }

    if (bytes_transfered != sizeof(*frame)) {
        gsusb_prepare_read(dev, urb_num);
        dev->last_error = GSUSB_ERR_READ_SIZE;
        return false;
    }

    memcpy(frame, dev->rxurbs[urb_num].buf, sizeof(*frame));

    return gsusb_prepare_read(dev, urb_num);
}

static bool gsusb_read_di(HDEVINFO hdi, SP_DEVICE_INTERFACE_DATA interfaceData, struct gsusb_device *dev)
{
    /* get required length first (this call always fails with an error) */
    ULONG requiredLength=0;
    SetupDiGetDeviceInterfaceDetail(hdi, &interfaceData, NULL, 0, &requiredLength, NULL);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        dev->last_error = GSUSB_ERR_SETUPDI_IF_DETAILS;
        return false;
    }

    PSP_DEVICE_INTERFACE_DETAIL_DATA detail_data =
        (PSP_DEVICE_INTERFACE_DETAIL_DATA) LocalAlloc(LMEM_FIXED, requiredLength);

    if (detail_data != NULL) {
        detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
    } else {
        dev->last_error = GSUSB_ERR_MALLOC;
        return false;
    }

    bool retval = true;
    ULONG length = requiredLength;
    if (!SetupDiGetDeviceInterfaceDetail(hdi, &interfaceData, detail_data, length, &requiredLength, NULL) ) {
        dev->last_error = GSUSB_ERR_SETUPDI_IF_DETAILS2;
        retval = false;
    } else if (FAILED(StringCchCopy(dev->path, sizeof(dev->path), detail_data->DevicePath))) {
        dev->last_error = GSUSB_ERR_PATH_LEN;
        retval = false;
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

    dev->last_error = GSUSB_ERR_OK;
    return true;
}

bool gsusb_find_devices(struct gsusb_device *buf, size_t buf_size, uint16_t *num_devices)
{

    GUID guid;
    if (CLSIDFromString(L"{c15b4308-04d3-11e6-b3ea-6057189e6443}", &guid) != NOERROR) {
        buf->last_error = GSUSB_ERR_CLSID;
        return false;
    }

    HDEVINFO hdi = SetupDiGetClassDevs(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hdi == INVALID_HANDLE_VALUE) {
        buf->last_error = GSUSB_ERR_GET_DEVICES;
        return false;
    }

    bool rv = false;
    *num_devices = 0;

    unsigned max_results = buf_size / sizeof(*buf);

    buf->last_error = GSUSB_ERR_OK;
    for (unsigned i=0; i<GSUSB_MAX_DEVICES; i++) {

        SP_DEVICE_INTERFACE_DATA interfaceData;
        interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        if (SetupDiEnumDeviceInterfaces(hdi, NULL, &guid, i, &interfaceData)) {

            if (i<max_results) {
                if (!gsusb_read_di(hdi, interfaceData, &buf[i])) {
                    buf->last_error = buf[i].last_error;
                    break;
                }
            }

        } else {
            DWORD err = GetLastError();
            if (err==ERROR_NO_MORE_ITEMS) {
                *num_devices = i;
                rv = true;
            } else {
                buf->last_error = GSUSB_ERR_SETUPDI_IF_ENUM;
            }
            break;
        }

    }

    SetupDiDestroyDeviceInfoList(hdi);

    return rv;
}

gsusb_err_t gsusb_last_error(struct gsusb_device *dev)
{
    return dev->last_error;
}
