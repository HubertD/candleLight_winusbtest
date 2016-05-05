#include <QCoreApplication>
#include "candle.h"

int main(int argc, char *argv[])
{

    candle_list_handle clist;
    candle_handle hdev;
    candle_frame_t frame;
    candle_devstate_t state;
    uint8_t num_devices;


    if (!candle_list_scan(&clist)) {
        printf("cannot scan for candle devices.\n");
        candle_list_free(clist);
        return -1;
    }

    candle_list_length(clist, &num_devices);

    if (num_devices==0) {
        printf("cannot find any candle devices.\n");
        return 0;
    }

    printf("detected %d candle device(s):\n", num_devices);
    for (unsigned i=0; i<num_devices; i++) {
        if (candle_dev_get(clist, i, &hdev)) {
            candle_dev_get_state(hdev, &state);

            uint8_t channels;
            candle_channel_count(hdev, &channels);
            printf("%d: state=%d interfaces=%d path=%S\n", i, state, channels, candle_dev_get_path(hdev));

            candle_dev_free(hdev);
        } else {
            printf("error getting info for device %d\n", i);
        }
    }

    if (!candle_dev_get(clist, 0, &hdev)) {
        printf("error getting info for device %d\n", 0);
        return -2;
    }

    if (!candle_dev_open(hdev)) {
        printf("could not open candle device (%d)\n", candle_dev_last_error(hdev));
        return -3;
    }

    if (!candle_channel_set_bitrate(hdev, 0, 500000)) {
        printf("could not set bitrate.\n");
        return -4;
    }

    if (!candle_channel_start(hdev, 0, 0)) {
        printf("could not start device.\n");
        return -5;
    }

    while (true) {

        if (candle_frame_read(hdev, &frame, 1000)) {

            if (candle_frame_type(&frame) == CANDLE_FRAMETYPE_RECEIVE) {

                uint8_t dlc = candle_frame_dlc(&frame);
                uint8_t *data = candle_frame_data(&frame);

                printf(
                   "%10d ID 0x%08x [%d]",
                   candle_frame_timestamp_us(&frame),
                   candle_frame_id(&frame),
                   dlc
                );
                for (int i=0; i<dlc; i++) {
                     printf(" %02X", data[i]);
                }
                printf("\n");

                frame.can_id += 1;
                candle_frame_send(hdev, 0, &frame);

            }


        } else {
            candle_err_t err = candle_dev_last_error(hdev);
            if (err == CANDLE_ERR_READ_TIMEOUT) {
                printf("Timeout waiting for CAN data\n");
            } else {
                printf("Error reading candle frame: %d\n", err);
                return -err;
            }
        }

    }

    return 0;
}
