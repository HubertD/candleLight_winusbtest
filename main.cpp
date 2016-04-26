#include <QCoreApplication>
#include <QTimer>

#include <winusb.h>
#include <winbase.h>
#include <stdio.h>
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>
#include <strsafe.h>

#include <stdbool.h>

#include "gsusb_def.h"
#include "ch_9.h"

#define MAX_DEVPATH_LENGTH 256

struct rx_urb {
    OVERLAPPED ovl;
    uint8_t buf[64];
};

struct gsusb_device {
    WINUSB_INTERFACE_HANDLE winUSBHandle;
    UCHAR interfaceNumber;
    UCHAR deviceSpeed;
    UCHAR bulkInPipe;
    UCHAR bulkOutPipe;

    struct rx_urb rxurbs[GS_MAX_RX_URBS];
    HANDLE rxevents[GS_MAX_RX_URBS];

};

BOOL GetDevicePath(LPGUID InterfaceGuid, wchar_t *DevicePath, size_t BufLen)
{
  BOOL bResult = FALSE;
  HDEVINFO deviceInfo;
  SP_DEVICE_INTERFACE_DATA interfaceData;
  PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = NULL;
  ULONG length;
  ULONG requiredLength=0;
  HRESULT hr;

  deviceInfo = SetupDiGetClassDevs(InterfaceGuid,
                     NULL, NULL,
                     DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

  bResult = SetupDiEnumDeviceInterfaces(deviceInfo,
                                        NULL,
                                        InterfaceGuid,
                                        0,
                                        &interfaceData);
  SetupDiGetDeviceInterfaceDetail(deviceInfo,
                                  &interfaceData,
                                  NULL, 0,
                                  &requiredLength,
                                  NULL);

  detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)
     LocalAlloc(LMEM_FIXED, requiredLength);

  if(NULL == detailData)
  {
    SetupDiDestroyDeviceInfoList(deviceInfo);
    return FALSE;
  }

  detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
  length = requiredLength;

  bResult = SetupDiGetDeviceInterfaceDetail(deviceInfo,
                                           &interfaceData,
                                           detailData,
                                           length,
                                           &requiredLength,
                                           NULL);

  if(FALSE == bResult)
  {
    LocalFree(detailData);
    return FALSE;
  }

  hr = StringCchCopy(DevicePath,
                     BufLen,
                     detailData->DevicePath);
  if(FAILED(hr))
  {
    SetupDiDestroyDeviceInfoList(deviceInfo);
    LocalFree(detailData);
  }

  LocalFree(detailData);

  return bResult;
}


HANDLE OpenDevice(LPGUID guid, BOOL bSync)
{
  HANDLE hDev = NULL;
  wchar_t devicePath[MAX_DEVPATH_LENGTH];
  BOOL retVal = GetDevicePath(guid, devicePath, sizeof(devicePath));
 //Error-handling code omitted.

  hDev = CreateFile(devicePath,
                    GENERIC_WRITE | GENERIC_READ,
                    FILE_SHARE_WRITE | FILE_SHARE_READ,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                    NULL);

  //Error-handling code omitted.
  return hDev;
}



BOOL Initialize_Device(struct gsusb_device *devInfo)
{
  BOOL bResult;
  WINUSB_INTERFACE_HANDLE usbHandle;
  USB_INTERFACE_DESCRIPTOR ifaceDescriptor;
  WINUSB_PIPE_INFORMATION pipeInfo;
  UCHAR speed;
  ULONG length;

  LPGUID _lpGuid = (LPGUID)malloc (sizeof(GUID));
  HRESULT result = CLSIDFromString (L"{c15b4308-04d3-11e6-b3ea-6057189e6443}", _lpGuid);

  HANDLE deviceHandle = OpenDevice(_lpGuid, TRUE);
  bResult = WinUsb_Initialize(deviceHandle, &usbHandle);

  if(bResult)
  {
    devInfo->winUSBHandle = usbHandle;
    length = sizeof(UCHAR);
    bResult = WinUsb_QueryDeviceInformation(devInfo->winUSBHandle,
                                            DEVICE_SPEED,
                                            &length,
                                            &speed);
  }

  if(bResult)
  {
    devInfo->deviceSpeed = speed;
    bResult = WinUsb_QueryInterfaceSettings(devInfo->winUSBHandle,
                                            0,
                                            &ifaceDescriptor);
  }
  if(bResult)
  {
    devInfo->interfaceNumber = ifaceDescriptor.bInterfaceNumber;

    for(int i=0;i<ifaceDescriptor.bNumEndpoints;i++)
    {
      bResult = WinUsb_QueryPipe(devInfo->winUSBHandle,
                                 0,
                                 (UCHAR) i,
                                 &pipeInfo);

      if(pipeInfo.PipeType == UsbdPipeTypeBulk &&
                  USB_ENDPOINT_DIRECTION_IN(pipeInfo.PipeId))
      {
        devInfo->bulkInPipe = pipeInfo.PipeId;
      }
      else if(pipeInfo.PipeType == UsbdPipeTypeBulk &&
                  USB_ENDPOINT_DIRECTION_OUT(pipeInfo.PipeId))
      {
        devInfo->bulkOutPipe = pipeInfo.PipeId;
      }
      else
      {
        bResult = FALSE;
        break;
      }
    }
  }

  return bResult;
}

bool usb_control_msg(WINUSB_INTERFACE_HANDLE hnd, uint8_t request, uint8_t requesttype, uint16_t value, uint16_t index, void *data, uint16_t size)
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

bool gsusb_set_host_format(struct gsusb_device *dev)
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

bool gsusb_get_device_info(struct gsusb_device *dev, struct gs_device_config *dconf)
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

bool gsusb_get_bittiming_const(struct gsusb_device *dev, uint16_t channel, struct gs_device_bt_const *data)
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

bool gsusb_prepare_read(struct gsusb_device *dev, unsigned urb_num)
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

bool gsusb_read_frame(struct gsusb_device *dev, struct gs_host_frame *frame, uint32_t timeout_ms)
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

int main(int argc, char *argv[])
{
    struct gsusb_device dev;
    BOOL ok = Initialize_Device(&dev);

    if (ok) {
        printf("found candleLight.\n");

        ok = gsusb_set_host_format(&dev);
        if (!ok) {
            printf("could not set host format.");
        }

        struct gs_device_config dconf;
        ok = gsusb_get_device_info(&dev, &dconf);
        if (ok) {
            struct gs_device_bittiming timing;
            // TODO set bitrate
            memset(dev.rxurbs, 0, sizeof(dev.rxurbs));
            for (unsigned i=0; i<GS_MAX_RX_URBS; i++) {
                dev.rxevents[i] = CreateEvent(NULL, true, false, NULL);
                dev.rxurbs[i].ovl.hEvent = dev.rxevents[i];
                gsusb_prepare_read(&dev, i);
            }
            ok = gsusb_set_device_mode(&dev, 0, GS_CAN_MODE_START, 0);

            while (true) {
                struct gs_host_frame frame;
                if (gsusb_read_frame(&dev, &frame, 1000)) {
                    if (frame.echo_id==0xFFFFFFFF) {
                        printf("Caught CAN frame: ID 0x%08x\n", frame.can_id);
                        frame.can_id += 1;
                        gsusb_send_frame(&dev, 0, &frame);
                    }

                } else {
                    printf("Timeout waiting for CAN data\n");
                }
            }


        }

        if (ok) {
            printf("device started.");
        }

    } else {
        printf("cannot find candleLight!\n");
    }
}
