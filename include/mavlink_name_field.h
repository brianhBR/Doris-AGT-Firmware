#ifndef MAVLINK_NAME_FIELD_H
#define MAVLINK_NAME_FIELD_H

#include <stddef.h>
#include <string.h>

#define MAVLINK_NAME_FIELD_LEN 10

// Copy a name into MAVLink's fixed char[10] field, zeroing the remainder.
//
// The generated packer copies all ten bytes straight out of the pointer it is
// handed, so passing a shorter string literal reads past the end of that
// literal and puts whatever follows it in .rodata on the wire behind the
// terminator. Measured on a vehicle, the AGT was advertising "AGT_CAP\0RE",
// "REL_STAT\0P" and "PWR_SHDN\0v" — the trailing bytes are the neighbouring
// name literals. A receiver that stops at the first NUL never notices, but one
// that strips NULs instead reads "AGT_CAPRE" and drops the message as unknown,
// which is exactly what left the extension with no AGT capability or release
// status. Copying through here keeps the padding clean for either reader.
static inline void mavlinkNameField(char field[MAVLINK_NAME_FIELD_LEN],
                                    const char* name) {
    memset(field, 0, MAVLINK_NAME_FIELD_LEN);
    if (name == NULL) {
        return;
    }
    for (size_t i = 0; i < MAVLINK_NAME_FIELD_LEN && name[i] != '\0'; i++) {
        field[i] = name[i];
    }
}

#endif // MAVLINK_NAME_FIELD_H
