#include <QCoreApplication>
#include <QTimer>

#include <winusb.h>
#include <stdio.h>
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>
#include <strsafe.h>

#include <stdbool.h>

#include "gs_usb.h"
#define MAX_DEVPATH_LENGTH 256

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

struct cl_info {
    WINUSB_INTERFACE_HANDLE winUSBHandle;
    UCHAR deviceSpeed;
    UCHAR bulkInPipe;
    UCHAR bulkOutPipe;
};


BOOL Initialize_Device(struct cl_info *devInfo)
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

bool sendInterfaceRequest(WINUSB_INTERFACE_HANDLE hnd, uint8_t bRequest, uint8_t wIndex, uint16_t wValue, uint8_t *buf, uint16_t len)
{
    if (hnd==INVALID_HANDLE_VALUE) {
        return false;
    }

    WINUSB_SETUP_PACKET packet;
    ZeroMemory(&packet, sizeof(WINUSB_SETUP_PACKET));

    packet.RequestType = 0x41; // Vendor Interface Request
    packet.Request = bRequest;
    packet.Value = wValue;
    packet.Index = wIndex;
    packet.Length = len;

    ULONG cbSent = 0;
    return WinUsb_ControlTransfer(hnd, packet, buf, len, &cbSent, 0);
}

int main(int argc, char *argv[])
{
    LPGUID _lpGuid = (LPGUID)malloc (sizeof(GUID));
    HRESULT result = CLSIDFromString (L"{c15b4308-04d3-11e6-b3ea-6057189e6443}", _lpGuid);

    struct cl_info devInfo;
    BOOL ok = Initialize_Device(&devInfo);

    if (ok) {
        printf("found candleLight.\n");

        struct gs_device_mode mode;
        mode.mode = GS_CAN_MODE_START;
        mode.flags = 0;
        if (sendInterfaceRequest(devInfo.winUSBHandle, GS_USB_BREQ_MODE, 0, 0, (uint8_t*)&mode, sizeof(struct gs_device_mode))) {
            printf("device started.");
        }

    } else {
        printf("cannot find candleLight!\n");
    }
}
