#include <QCoreApplication>
#include "gsusb.h"

int main(int argc, char *argv[])
{

    struct gsusb_device_info devs[16];
    uint16_t num_devices;

    if (!gsusb_find_devices(devs, sizeof(devs), &num_devices)) {
        printf("cannot enumerate candleLight devices.\n");
        return -1;
    } else {
        printf("detected %d candleLight device(s):\n", num_devices);
        for (unsigned i=0; i<num_devices; i++) {
            struct gsusb_device_info *dev = &devs[i];
            printf("%d: state=%d ch=%d path=%S\n", i, dev->state, dev->channels, dev->path);
        }
        return 0;
    }


    wchar_t path[512];
    struct gsusb_device dev;
    //struct gs_device_bittiming timing;
    struct gs_host_frame frame;

    if (!gsusb_get_device_path(path, sizeof(path))) {
        printf("candleLight device not found.\n");
        return -1;
    } else {
        printf("found candleLight device: %S\n", path);
    }

    if (!gsusb_open(&dev, path)) {
        printf("could not open candleLight device.\n");
        return -2;
    }

    // TODO set bitrate

    if (!gsusb_set_device_mode(&dev, 0, GS_CAN_MODE_START, 0)) {
        printf("could not start device.\n");
        return -3;
    }

    while (true) {

        if (gsusb_recv_frame(&dev, &frame, 1000)) {

            if (frame.echo_id!=0xFFFFFFFF) {
                continue;
            }

            printf("Caught CAN frame: ID 0x%08x [%d]", frame.can_id, frame.can_dlc);
            for (int i=0; i<frame.can_dlc; i++) {
                 printf(" %02X", frame.data[i]);
            }
            printf("\n");
            frame.can_id += 1;
            gsusb_send_frame(&dev, 0, &frame);

        } else {
            printf("Timeout waiting for CAN data\n");
        }

    }

    return 0;
}
