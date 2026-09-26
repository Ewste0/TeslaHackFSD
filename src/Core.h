// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TIGARD_MAPS_CORE_H
#define TIGARD_MAPS_CORE_H

#include <QString>
#include <ftdi.h>

#if defined(__APPLE__) || defined(Q_OS_MAC)
#include <libusb.h>
#elif defined(Q_OS_WIN)
#include <libusb.h>
#else
#include <libusb-1.0/libusb.h>
#endif

class SysCore {
private:
    static inline QString cachedTigardSerial;

public:
    static QString tigardSerial() {
        if (!cachedTigardSerial.isEmpty()) {
            return cachedTigardSerial;
        }

        ftdi_context *ftdi = ftdi_new();
        if (!ftdi) {
            return "FTDI_ERROR";
        }

        if (ftdi_usb_open(ftdi, 0x0403, 0x6010) < 0) {
            ftdi_free(ftdi);
            return "NOT_CONNECTED";
        }

        char manufacturer[128] = {};
        char description[128] = {};
        char serial[128] = {};
        const int result = ftdi_usb_get_strings(
            ftdi,
            libusb_get_device(ftdi->usb_dev),
            manufacturer,
            sizeof(manufacturer),
            description,
            sizeof(description),
            serial,
            sizeof(serial));

        ftdi_usb_close(ftdi);
        ftdi_free(ftdi);

        if (result < 0 || serial[0] == '\0') {
            return "READ_ERROR";
        }

        cachedTigardSerial = QString::fromLatin1(serial);
        return cachedTigardSerial;
    }

    // Compatibility wrapper for the original UI code.
    static QString g_t_s() {
        return tigardSerial();
    }
};

#endif
