#include <QCoreApplication>
#include "gsusb.h"

int main(int argc, char *argv[])
{
    wchar_t path[512];
    struct gsusb_device dev;
    //struct gs_device_bittiming timing;
    struct gs_host_frame frame;

    if (!gsusb_get_device_path(path, sizeof(path))) {
        printf("candleLight device not found.\n");
        return -1;
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

            printf("Caught CAN frame: ID 0x%08x\n", frame.can_id);
            frame.can_id += 1;
            gsusb_send_frame(&dev, 0, &frame);

        } else {
            printf("Timeout waiting for CAN data\n");
        }

    }

    return 0;
}
