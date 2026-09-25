#ifndef K380_HID_H
#define K380_HID_H

typedef enum k380_key_mode {
    K380_MODE_FN_LOCKED,
    K380_MODE_MEDIA_KEYS
} k380_key_mode;

typedef enum k380_apply_result {
    K380_APPLY_OK = 0,
    K380_APPLY_NOT_FOUND = 1,
    K380_APPLY_ERROR = 2
} k380_apply_result;

/* quiet suppresses messages for background retries. */
k380_apply_result k380_apply_key_mode(k380_key_mode mode, int quiet);

#endif
