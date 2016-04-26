#include <QCoreApplication>
#include <QTimer>

#include "gsusb.h"

int main(int argc, char *argv[])
{
    struct gsusb_device dev;
    struct gs_device_config dconf;
    struct gs_device_bittiming timing;

    wchar_t path[512];
    if (!gsusb_get_device_path(path, sizeof(path))) {
        printf("candleLight device not found.\n");
        return -1;
    }

    if (!gsusb_open(&dev, path)) {
        printf("could not open candleLight device.\n");
        return -2;
    }

    if (!gsusb_set_host_format(&dev)) {
        printf("could not set host format.\n");
        return -3;
    }

    if (!gsusb_get_device_info(&dev, &dconf)) {
        printf("could not read device info.\n");
        return -4;
    }

    // TODO set bitrate

    memset(dev.rxurbs, 0, sizeof(dev.rxurbs));
    for (unsigned i=0; i<GS_MAX_RX_URBS; i++) {
        dev.rxevents[i] = CreateEvent(NULL, true, false, NULL);
        dev.rxurbs[i].ovl.hEvent = dev.rxevents[i];
        gsusb_prepare_read(&dev, i);
    }

    if (!gsusb_set_device_mode(&dev, 0, GS_CAN_MODE_START, 0)) {
        printf("could not start device.\n");
        return -5;
    }

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

    return 0;
}
