#include <stdio.h>
#include <hidapi.h>

#include "k380_hid.h"

#define K380_VID 0x046d
#define K380_PID 0xb342
#define K380_USAGE_PAGE 0xff00
#define K380_USAGE 0x0001
#define K380_REPORT_SIZE 7

static const unsigned char fn_keys_report[K380_REPORT_SIZE] = {
    0x10, 0xff, 0x0b, 0x1e, 0x00, 0x00, 0x00
};
static const unsigned char media_keys_report[K380_REPORT_SIZE] = {
    0x10, 0xff, 0x0b, 0x1e, 0x01, 0x00, 0x00
};

static int write_report(const unsigned char *report, int quiet)
{
    struct hid_device_info *devices = hid_enumerate(K380_VID, K380_PID);
    struct hid_device_info *device;
    int found = 0;
    int failed = 0;

    for (device = devices; device != NULL; device = device->next) {
        hid_device *handle;
        int written;

        if (device->usage_page != K380_USAGE_PAGE || device->usage != K380_USAGE)
            continue;

        found = 1;
        if (device->path == NULL) {
            failed = 1;
            if (!quiet)
                fputs("K380 HID interface has no path\n", stderr);
            continue;
        }
        handle = hid_open_path(device->path);
        if (handle == NULL) {
            failed = 1;
            if (!quiet)
                fputs("Cannot open K380 HID interface\n", stderr);
            continue;
        }

        written = hid_write(handle, report, K380_REPORT_SIZE);
        if (written != K380_REPORT_SIZE) {
            failed = 1;
            if (!quiet)
                fprintf(stderr, "Cannot set K380 mode (%d bytes written)\n", written);
        }
        hid_close(handle);
    }

    hid_free_enumeration(devices);
    return !found ? K380_APPLY_NOT_FOUND : (failed ? K380_APPLY_ERROR : K380_APPLY_OK);
}

k380_apply_result k380_apply_key_mode(k380_key_mode mode, int quiet)
{
    return write_report(mode == K380_MODE_FN_LOCKED ? fn_keys_report : media_keys_report, quiet);
}
