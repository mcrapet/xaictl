/*
 *  SteelSeries XAI mouse configuration tool
 *  Copyright (c) 2010 Matthieu Crapet <mcrapet@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <dirent.h>
#include <usb.h>

#define XAI_MOUSE_PROGRAM_NAME    "xaictl"
#define XAI_MOUSE_PROGRAM_VERSION "1.1"

/*
 * SteelSeries defines & low-level protocol
 */
#define XAI_MOUSE_VENDOR_ID        0x1038
#define XAI_MOUSE_PRODUCT_ID       0x1360
#define XAI_MOUSE_INTERFACE_NUM         2
#define XAI_MOUSE_PROFILE_NUM           5
#define XAI_MOUSE_BUTTON_NUM            9

/* Thresholds */
#define XAI_MOUSE_CPI_MIN             100
#define XAI_MOUSE_CPI_MAX            5001
#define XAI_MOUSE_RATE_MIN            125
#define XAI_MOUSE_RATE_MAX           1000
#define XAI_MOUSE_ACCEL_MIN             0
#define XAI_MOUSE_ACCEL_MAX           100
#define XAI_MOUSE_LCD_BRIGHTNESS_MIN    1
#define XAI_MOUSE_LCD_BRIGHTNESS_MAX   10
#define XAI_MOUSE_LCD_CONTRAST_MIN      1
#define XAI_MOUSE_LCD_CONTRAST_MAX     40
#define XAI_MOUSE_FREEMOVE_MIN          0
#define XAI_MOUSE_FREEMOVE_MAX         10
#define XAI_MOUSE_AIM_MIN               0
#define XAI_MOUSE_AIM_MAX              10

#define XAI_MOUSE_LL_DATA_LENGTH           (64 - 6*sizeof(unsigned char))
#define XAI_MOUSE_LL_SET_PROFILE_SETTINGS  0x03
#define XAI_MOUSE_LL_GET_PROFILE_SETTINGS  0x04
#define XAI_MOUSE_LL_SET_CURRENT_PROFILE   0x0C
#define XAI_MOUSE_LL_GET_CURRENT_PROFILE   0x0D
#define XAI_MOUSE_LL_PING_OR_ACK           0x14
#define XAI_MOUSE_LL_PONG_OR_RES           0x15
#define XAI_MOUSE_LL_SET_PROFILE_NAME      0x17
#define XAI_MOUSE_LL_GET_PROFILE_NAME      0x1A
#define XAI_MOUSE_LL_SAVE_TO_FLASH         0x24

struct xai_ll_message_header
{
   unsigned char  null_byte;
   unsigned char  operation;
   unsigned char  id;
   unsigned char  part;
   unsigned char  argument1;
   unsigned char  argument2; /* numbers of items in data written? */
};

struct xai_ll_message
{
    struct xai_ll_message_header header;
    union {
        char data[XAI_MOUSE_LL_DATA_LENGTH];

        struct {
            unsigned char  __unknown1__[3];   // 64 64 64
            unsigned short rate;              /* ExactRate (Hz) */
            unsigned char  __unknown2__[5];   // 00 00 03 06 01
            unsigned char  accel;             /* ExactAccel */
            unsigned char  __unknown3__;      // 06
            unsigned char  freemove;          /* FreeMode */
            unsigned char  aim;               /* ExactAim */
            unsigned char  brightness;        /* LCD brightness */
            unsigned char  contrast;          /* LCD constrast */
        } __attribute__((__packed__)) part1;

        struct {
            unsigned short cpi1;              /* ExactSense CPI1 (led off) */
            unsigned short cpi2;              /* ExactSense CPI2 (led on) */
        } __attribute__((__packed__)) part2;

        struct {
            unsigned short button1;
            unsigned short button2;
            unsigned short button3;
            unsigned short button4;
            unsigned short button5;
            unsigned short button6;
            unsigned short button7;
            unsigned short __unknown1__;      // 07 00
            unsigned short lr_handed_mode;    // 0D 00 or 7F 00
            unsigned short __unknown2__;      // 0D 00
            unsigned short __unknown3__;      // 0D 00
            unsigned short button8;
            unsigned short button9;
        } __attribute__((__packed__)) part3;

    } u;
} __attribute__((__packed__));


/* USB related */
#define PACKET_SIZE             64
#define PACKET_TIMEOUT        1000
#define PACKET_WRITE             0
#define PACKET_READ              1

/* Return values */
#define RET_OK                     0
#define RET_ERROR_WRONG_PARAMETER -1
#define RET_ERROR_NO_DEVICE_FOUND -2
#define RET_ERROR_NO_PERMISSION   -3
#define RET_ERROR_SYSTEM          -5 /* unknown system error */
#define RET_ERROR_BUS             -4 /* unknown USB error */

/* Mask for 'fields' */
#define PROFILE_FIELD_MASK           0x400000FF
#define PROFILE_FIELD_NAME           0x40000100
#define PROFILE_FIELD_CPI1           0x40000200
#define PROFILE_FIELD_CPI2           0x40000400
#define PROFILE_FIELD_RATE           0x40000800
#define PROFILE_FIELD_AIM            0x40001000
#define PROFILE_FIELD_ACCEL          0x40002000
#define PROFILE_FIELD_FREEMOVE       0x40004000
#define PROFILE_FIELD_LCD_BRIGHTNESS 0x40008000
#define PROFILE_FIELD_LCD_CONTRAST   0x40010000
#define PROFILE_FIELD_BUTTON_1       0x40020000
#define PROFILE_FIELD_BUTTON_2       0x40030000
#define PROFILE_FIELD_BUTTON_3       0x40040000
#define PROFILE_FIELD_BUTTON_4       0x40080000
#define PROFILE_FIELD_BUTTON_5       0x40100000
#define PROFILE_FIELD_BUTTON_6       0x40200000
#define PROFILE_FIELD_BUTTON_7       0x40400000
#define PROFILE_FIELD_BUTTON_8       0x40800000
#define PROFILE_FIELD_BUTTON_9       0x41000000

struct xai_profile
{
    unsigned long fields;

    char name[PACKET_SIZE];
    short rate;
    short aim;
    short accel;
    short freemove;
    short lcd_brightness;
    short lcd_contrast;
    unsigned short cpi[2];
    unsigned short button[XAI_MOUSE_BUTTON_NUM];
};

struct xai_context
{
    usb_dev_handle *dev;
    char usbhid_driver_intf[16]; /* for example: "4-2:1.2" */

    struct xai_profile p[XAI_MOUSE_PROFILE_NUM];
    unsigned char cur_id;
    unsigned char cur_index;     /* 0-based profile index */

    /* command lines options */
    int usb_debug;
    int usb_rebind;
    int set_current_profile;
};


/*
 * Const static data, declarations
 */

static const char *button_setup[14] = {
    "User macro (?)",
    "0100??",
    "Tilt Left",
    "Tilt Right",
    "IE Forward",
    "IE Backward",
    "Middle Click",
    "0700??",
    "0800??",
    "Left Click",
    "Right Click",
    "Mouse Wheel Up",
    "Mouse Wheel Down",
    "Disable"
};

/* local prototypes */
static int xai_usbhid_find_interface (int, int, int, char *, size_t);
static int xai_usbhid_driver_workaround (char *, int);

static int xai_init (int, int, int, struct xai_context *);
static int xai_uninit (struct xai_context *);

static int xai_device_transfer_packet(usb_dev_handle *, unsigned char [], int);
static int xai_device_read_packet(usb_dev_handle *, struct xai_ll_message_header *,
        struct xai_ll_message *);
static int xai_device_write_packet(usb_dev_handle *, struct xai_ll_message *);

static int xai_device_packet_print (FILE *, unsigned char [], int);
static int xai_device_init (struct xai_context *);
static int xai_device_write_to_flash (struct xai_context *);

static int xai_profile_get_config (struct xai_context *, int, struct xai_profile *);
static int xai_profile_set_config (struct xai_context *, int, struct xai_profile *);
static int xai_profile_get_current_index (struct xai_context *, int *);
static int xai_profile_set_current_index (struct xai_context *, int);
static int xai_profile_get_name (struct xai_context *, int, struct xai_profile *);
static int xai_profile_print (FILE *, struct xai_profile *, int);
static int xai_profile_change_req (struct xai_profile *, unsigned long, char *);


/*
 * Linux kernel takes exclusive ownership of all interfaces.
 * Recurse sub-directories to find proper device string, then unbind interface.
 */
static int xai_usbhid_find_interface (int vendor_id, int product_id,
        int interface, char *intf_name, size_t intf_length)
{
    static const char *sysfs_path = "/sys/bus/usb/drivers/usbhid";
    struct dirent *f;
    DIR *d = opendir(sysfs_path);
    FILE *fp;
    char buffer[255];
    int found = 0;
    int ret = RET_ERROR_NO_DEVICE_FOUND;

    if (!d)
        return RET_ERROR_SYSTEM;

    while ((f = readdir(d)) != NULL) {
        size_t len = strlen(f->d_name);

        if ((len >= 7) && (f->d_name[len - 1] == 0x30)) {
            char id[5];

            snprintf(buffer, sizeof(buffer), "%s/%s/uevent", sysfs_path,
                    f->d_name);
            fp = fopen(buffer, "r");
            if (fp) {

                while (fgets(buffer, sizeof(buffer), fp)) {
                    if (!strncmp(buffer, "PRODUCT=", 8)) {
                        snprintf(id, sizeof(id), "%x", vendor_id);
                        if (strstr(buffer, id) != 0) {
                            snprintf(id, sizeof(id), "%x", product_id);
                            if (strstr(buffer, id) != 0) {
                                f->d_name[len - 1] = 0x30 +
                                    XAI_MOUSE_INTERFACE_NUM;
                                strncpy(intf_name, f->d_name, intf_length);
                                found = 1;
                                break;
                            }
                        }
                    }
                } //while

                fclose(fp);

                if (found) {
                    ret = RET_OK;
                    break;
                }
            }
        }
    } //while

    closedir(d);
    return ret;
}

/*
 * Bind or unbind specified interface.
 */
static int xai_usbhid_driver_workaround (char *intf_name, int bind)
{
    const char *sysfs_path = (bind) ?
        "/sys/bus/usb/drivers/usbhid/bind" :
        "/sys/bus/usb/drivers/usbhid/unbind";

    int ret = RET_ERROR_WRONG_PARAMETER;

    if (*intf_name != '\0') {
        FILE *fp = fopen(sysfs_path, "w");

        ret = RET_ERROR_NO_PERMISSION;
        if (fp) {
            if (fputs(intf_name, fp) > 0) {
                fprintf(stderr, "echo %s > %s\n", intf_name, sysfs_path);
                ret = RET_OK;
            }
            fclose(fp);
        }
    }

    return ret;
}


/*
 * Initialize communication channel (libusb stuff) with device.
 */
static int xai_init (int vendor_id, int product_id, int interface,
        struct xai_context *ctx)
{
    struct usb_bus *bus;
    struct usb_device *dev;
    int ret = RET_OK;

    usb_init();
    usb_find_busses();
    usb_find_devices();

    for (bus = usb_get_busses(); bus != NULL; bus = bus->next)
        for (dev = bus->devices; dev != NULL; dev = dev->next)
            if (dev->descriptor.idVendor == vendor_id &&
                    dev->descriptor.idProduct == product_id) {
                ctx->dev = usb_open(dev);
            }

    if (ctx->dev) {
        *ctx->usbhid_driver_intf = 0;
        xai_usbhid_find_interface (vendor_id, product_id,
                interface, &ctx->usbhid_driver_intf[0],
                sizeof(ctx->usbhid_driver_intf));

        if (usb_claim_interface(ctx->dev, interface) < 0) {
            if ((ret = xai_usbhid_driver_workaround(ctx->usbhid_driver_intf, 0))
                    == RET_OK) {
                if (usb_claim_interface(ctx->dev, interface) < 0) {
                    fprintf(stderr, "err: usb_claim_interface: %s\n",
                            usb_strerror());
                } else {
                    return ret;
                }
            }

            usb_close(ctx->dev);
            ctx->dev = NULL;
        }
    } else {
        ret = RET_ERROR_NO_DEVICE_FOUND;
    }

    return ret;
}

/*
 * Uninitialize communication channel (libusb stuff) with device.
 */
static int xai_uninit (struct xai_context *ctx)
{
    usb_release_interface(ctx->dev, XAI_MOUSE_INTERFACE_NUM);

    if (*ctx->usbhid_driver_intf != 0 && ctx->usb_rebind != 0)
        if (xai_usbhid_driver_workaround(ctx->usbhid_driver_intf, 1) != RET_OK)
            fprintf(stderr, "err: can rebind interface, no permission\n");

    usb_close(ctx->dev);
    return RET_OK;
}


/*
 * Control transfer message (libusb stuff)
 * direction := (PACKET_READ | PACKET_WRITE)
 */
static int xai_device_transfer_packet(usb_dev_handle *dev,
        unsigned char packet[PACKET_SIZE], int direction)
{
    int ret = RET_OK;

    if (direction == PACKET_READ)
        memset(&packet[0], 0x55, PACKET_SIZE);

    if (usb_control_msg(dev,
                (direction == PACKET_WRITE) ? 0x21 : 0xA1, // HID class
                (direction == PACKET_WRITE) ? 0x09 : 0x01, // SetReport / GetReport
                0x0300,                                    // Feature
                XAI_MOUSE_INTERFACE_NUM,
                (char *)packet, PACKET_SIZE, PACKET_TIMEOUT) < 0) {
        fprintf(stderr, "err send: %s\n", usb_strerror());
        ret = RET_ERROR_BUS;
    }

    return ret;
}

/* For reading a message (64 bytes), we need 2 writes + 2 reads */
static int xai_device_read_packet(usb_dev_handle *dev,
        struct xai_ll_message_header *in, struct xai_ll_message *out)
{
    int ret;

    memset(out, 0, sizeof(struct xai_ll_message));
    out->header.operation = XAI_MOUSE_LL_PING_OR_ACK;
    out->header.id        = in->id;
    out->header.part      = 0;
    out->header.argument1 = 0;

    //ret = xai_device_transfer_packet(dev, (unsigned char *)out, PACKET_WRITE);
    ret = RET_OK;
    if (ret == RET_OK) {
        memset(out, 0, sizeof(struct xai_ll_message));
        out->header.operation = in->operation;
        out->header.id        = in->id;
        out->header.part      = in->part;
        out->header.argument1 = in->argument1;

        ret = xai_device_transfer_packet(dev, (unsigned char *)out, PACKET_WRITE);
        if (ret == RET_OK) {

            // ret = xai_device_transfer_packet(dev, (unsigned char *)out, PACKET_READ);
            ret = RET_OK;
            if (ret == RET_OK) {

                //if (in->id != out->id) {
                //    fprintf(stdout, "ID %X / %X\n", in->id, out->id);
                //    return RET_ERROR_BUS;
                //}

                ret = xai_device_transfer_packet(dev, (unsigned char *)out,
                        PACKET_READ);

                if (out->header.operation != XAI_MOUSE_LL_PONG_OR_RES) {
                    usleep(5000);
                    ret = xai_device_transfer_packet(dev, (unsigned char *)out, PACKET_READ);
                    //return RET_ERROR_BUS;
                }
            }
        }
    }

    return ret;
}

/* For writing a message (64 bytes), we need 1 write + 1 read */
static int xai_device_write_packet(usb_dev_handle *dev,
        struct xai_ll_message *in)
{
    int ret = RET_ERROR_BUS;

    ret = xai_device_transfer_packet(dev, (unsigned char *)in, PACKET_WRITE);
    if (ret == RET_OK) {
        ret = xai_device_transfer_packet(dev, (unsigned char *)in, PACKET_READ);
        if (ret == RET_OK) {
            if (in->header.operation != XAI_MOUSE_LL_PING_OR_ACK) {
                usleep(5000);
                ret = xai_device_transfer_packet(dev, (unsigned char *)in,
                        PACKET_READ);
            }
        }
    }

    return ret;
}

static int xai_device_packet_print (FILE *out, unsigned char packet[PACKET_SIZE],
        int host_to_device)
{
    int i;

    if (host_to_device)
        fputs("> ", out);
    else
        fputs("< ", out);

    for (i = 0; i < 32; i++) {
        fprintf(out, "%02X ", packet[i]);
    }
    fputs("\n", out);
    return RET_OK;
}

/* Read entire mouse configuration */
static int xai_device_init (struct xai_context *ctx)
{
    int i, tries;
    unsigned char packet[PACKET_SIZE];

    static const unsigned char init_string[35] = {
        0x00, 0x13, 0x01, 0x47, 0x45, 0x47, 0x4a, 0x47, 0x59, 0x49, 0x4b, 0x44,
        0x43, 0x47, 0x42, 0x45, 0x39, 0x44, 0x57, 0x44, 0x4b, 0x42, 0x32, 0x37,
        0x45, 0x41, 0x41, 0x37, 0x4b, 0x39, 0x5a, 0x35, 0x4A, 0x31, 0x50 };

    memset(&packet[0], 0, PACKET_SIZE);
    memcpy(&packet[0], &init_string[0], sizeof(init_string));

    if (xai_device_transfer_packet(ctx->dev, packet, PACKET_WRITE) == RET_OK) {
        ctx->cur_id = 0x77;

        for (i = 0; i < XAI_MOUSE_PROFILE_NUM; i++) {
            tries = 3;
            while ((xai_profile_get_name(ctx, i, &ctx->p[i]) != RET_OK) &&
                    (tries--));

            if (tries == 0)
                goto device_init_err;
        }

        for (i = 0; i < XAI_MOUSE_PROFILE_NUM; i++) {
            tries = 3;
            while ((xai_profile_get_config(ctx, i, &ctx->p[i]) != RET_OK) &&
                    (tries--));

            if (tries == 0)
                goto device_init_err;
        }

        i = XAI_MOUSE_PROFILE_NUM; // out of bound index
        if (xai_profile_get_current_index(ctx, &i) != RET_OK)
            goto device_init_err;

        ctx->cur_index = (unsigned char)i;
        return RET_OK;
    }

device_init_err:
    return RET_ERROR_BUS;
}

/*
 * Save all settings to flash (current profile index, ...)
 */
static int xai_device_write_to_flash (struct xai_context *ctx)
{
    struct xai_ll_message msg;
    int ret;

    memset(&msg, 0, sizeof(struct xai_ll_message));

    msg.header.operation = XAI_MOUSE_LL_SAVE_TO_FLASH;
    msg.header.id = ctx->cur_id;
    ret = xai_device_write_packet(ctx->dev, &msg);

    if (ctx->usb_debug)
        xai_device_packet_print(stderr, (unsigned char *)&msg, 0);

    return ret;
}


/*
 * Fill xai_profile structure : configuration settings
 * \param[in] index 0-based profile number
 */
static int xai_profile_get_config (struct xai_context *ctx, int index,
        struct xai_profile *profile)
{
    struct xai_ll_message msg;
    struct xai_ll_message_header hdr;
    int ret;

    /* Part 1 */
    hdr.null_byte = 0;
    hdr.operation = XAI_MOUSE_LL_GET_PROFILE_SETTINGS;
    hdr.id = ctx->cur_id;
    hdr.part = 1;
    hdr.argument1 = (unsigned char)index;
    hdr.argument2 = 0;
    ret = xai_device_read_packet(ctx->dev, &hdr, &msg);

    if (ctx->usb_debug)
        xai_device_packet_print(stderr, (unsigned char *)&msg, 0);

    if (ret == RET_OK) {
        ctx->cur_id = msg.header.id;
        profile->rate = msg.u.part1.rate;
        profile->aim = msg.u.part1.aim;
        profile->accel = msg.u.part1.accel;
        profile->freemove = msg.u.part1.freemove;
        profile->lcd_brightness = msg.u.part1.brightness;
        profile->lcd_contrast = msg.u.part1.contrast;

        /* Part 2 */
        hdr.null_byte = 0;
        hdr.operation = XAI_MOUSE_LL_GET_PROFILE_SETTINGS;
        hdr.id = ctx->cur_id;
        hdr.part = 2;
        hdr.argument1 = (unsigned char)index;
        hdr.argument2 = 0;
        ret = xai_device_read_packet(ctx->dev, &hdr, &msg);

        if (ctx->usb_debug)
            xai_device_packet_print(stderr, (unsigned char *)&msg, 0);

        if (ret == RET_OK) {
            ctx->cur_id = msg.header.id;
            profile->cpi[0] = msg.u.part2.cpi1;
            profile->cpi[1] = msg.u.part2.cpi2;

            /* Part 3 */
            hdr.null_byte = 0;
            hdr.operation = XAI_MOUSE_LL_GET_PROFILE_SETTINGS;
            hdr.id = ctx->cur_id;
            hdr.part = 3;
            hdr.argument1 = (unsigned char)index;
            hdr.argument2 = 0;
            ret = xai_device_read_packet(ctx->dev, &hdr, &msg);

            if (ctx->usb_debug)
                xai_device_packet_print(stderr, (unsigned char *)&msg, 0);

            if (ret == RET_OK) {
                ctx->cur_id = msg.header.id;
                profile->button[0] = msg.u.part3.button1;
                profile->button[1] = msg.u.part3.button2;
                profile->button[2] = msg.u.part3.button3;
                profile->button[3] = msg.u.part3.button4;
                profile->button[4] = msg.u.part3.button5;
                profile->button[5] = msg.u.part3.button6;
                profile->button[6] = msg.u.part3.button7;
                profile->button[7] = msg.u.part3.button8;
                profile->button[8] = msg.u.part3.button9;
            }
        }
    }

    return RET_OK;
}

/*
 * Fill xai_profile structure : profile name
 * In the Windows tool, profile string length cannot exceed 11 characters.
 * \param[in] index 0-based profile number
 */
static int xai_profile_get_name (struct xai_context *ctx, int index,
        struct xai_profile *profile)
{
    struct xai_ll_message msg;
    struct xai_ll_message_header hdr;
    int ret;

    hdr.null_byte = 0;
    hdr.operation = XAI_MOUSE_LL_GET_PROFILE_NAME;
    hdr.id = ctx->cur_id;
    hdr.part = 0;
    hdr.argument1 = (unsigned char)index;
    hdr.argument2 = 0;
    ret = xai_device_read_packet(ctx->dev, &hdr, &msg);

    if (ctx->usb_debug)
        xai_device_packet_print(stderr, (unsigned char *)&msg, 0);

    if (ret == RET_OK) {
        ctx->cur_id = msg.header.id;
        if (msg.u.data[0] == '\0')
            profile->name[0] = 0;
        else
            strncpy(profile->name, &msg.u.data[0],
                    XAI_MOUSE_LL_DATA_LENGTH);
    }
    return ret;
}

/*
 * Modify profile configuration according to xai_profile structure
 * \param[in] index 0-based profile number
 */
static int xai_profile_set_config (struct xai_context *ctx, int index,
        struct xai_profile *profile)
{
    struct xai_ll_message msg;
    struct xai_ll_message_header hdr;
    int touch_flag, ret;

    /* Part 1 */
    touch_flag = 0;
    hdr.null_byte = 0;
    hdr.operation = XAI_MOUSE_LL_GET_PROFILE_SETTINGS;
    hdr.id = ctx->cur_id + 1;
    hdr.part = 1;
    hdr.argument1 = (unsigned char)index;
    hdr.argument2 = 0;
    ret = xai_device_read_packet(ctx->dev, &hdr, &msg);

    if (ctx->usb_debug)
        xai_device_packet_print(stderr, (unsigned char *)&msg, 0);

    if (ret == RET_OK) {
        ctx->cur_id = msg.header.id;

        if ((profile->fields & PROFILE_FIELD_RATE) == PROFILE_FIELD_RATE) {
            msg.u.part1.rate = profile->rate;
            touch_flag = 1;
        }
        if ((profile->fields & PROFILE_FIELD_AIM) == PROFILE_FIELD_AIM) {
            msg.u.part1.aim = profile->aim;
            touch_flag = 1;
        }
        if ((profile->fields & PROFILE_FIELD_ACCEL) == PROFILE_FIELD_ACCEL) {
            msg.u.part1.accel = profile->accel;
            touch_flag = 1;
        }
        if ((profile->fields & PROFILE_FIELD_FREEMOVE) ==
                PROFILE_FIELD_FREEMOVE) {
            msg.u.part1.freemove = profile->freemove;
            touch_flag = 1;
        }
        if ((profile->fields & PROFILE_FIELD_LCD_BRIGHTNESS) ==
                PROFILE_FIELD_LCD_BRIGHTNESS) {
            msg.u.part1.brightness = profile->lcd_brightness;
            touch_flag = 1;
        }
        if ((profile->fields & PROFILE_FIELD_LCD_CONTRAST) ==
                PROFILE_FIELD_LCD_CONTRAST) {
            msg.u.part1.contrast = profile->lcd_contrast;
            touch_flag = 1;
        }

        if (touch_flag) {
            msg.header.operation = XAI_MOUSE_LL_SET_PROFILE_SETTINGS;

            if (ctx->usb_debug)
                xai_device_packet_print(stderr, (unsigned char *)&msg, 1);

            ret = xai_device_write_packet(ctx->dev, &msg);

            if (ctx->usb_debug)
                xai_device_packet_print(stderr, (unsigned char *)&msg, 0);
        }

        if (ret == RET_OK)  {

            /* Part 2 */
            touch_flag = 0;
            hdr.null_byte = 0;
            hdr.operation = XAI_MOUSE_LL_GET_PROFILE_SETTINGS;
            hdr.id = ctx->cur_id + 1;
            hdr.part = 2;
            hdr.argument1 = (unsigned char)index;
            hdr.argument2 = 0;
            ret = xai_device_read_packet(ctx->dev, &hdr, &msg);

            if (ctx->usb_debug)
                xai_device_packet_print(stderr, (unsigned char *)&msg, 0);

            if (ret == RET_OK) {
                ctx->cur_id = msg.header.id;

                if ((profile->fields & PROFILE_FIELD_CPI1) ==
                        PROFILE_FIELD_CPI1) {
                    msg.u.part2.cpi1 = profile->cpi[0];
                    touch_flag = 1;
                }
                if ((profile->fields & PROFILE_FIELD_CPI2) ==
                        PROFILE_FIELD_CPI2) {
                    msg.u.part2.cpi2 = profile->cpi[1];
                    touch_flag = 1;
                }

                if (touch_flag) {
                    msg.header.operation = XAI_MOUSE_LL_SET_PROFILE_SETTINGS;

                    if (ctx->usb_debug)
                        xai_device_packet_print(stderr, (unsigned char *)&msg, 1);

                    ret = xai_device_write_packet(ctx->dev, &msg);

                    if (ctx->usb_debug)
                        xai_device_packet_print(stderr, (unsigned char *)&msg, 0);
                }

                if (ret == RET_OK)  {

                    /* Part 3 */
                    touch_flag = 0;
                    hdr.null_byte = 0;
                    hdr.operation = XAI_MOUSE_LL_GET_PROFILE_SETTINGS;
                    hdr.id = ctx->cur_id;
                    hdr.part = 3;
                    hdr.argument1 = (unsigned char)index;
                    hdr.argument2 = 0;
                    ret = xai_device_read_packet(ctx->dev, &hdr, &msg);

                    if (ctx->usb_debug)
                        xai_device_packet_print(stderr, (unsigned char *)&msg, 0);

                    if (ret == RET_OK) {
                        ctx->cur_id = msg.header.id;

                        if ((profile->fields & PROFILE_FIELD_BUTTON_1) ==
                                PROFILE_FIELD_BUTTON_1) {
                            msg.u.part3.button1 = profile->button[0];
                            touch_flag = 1;
                        }
                        if ((profile->fields & PROFILE_FIELD_BUTTON_2) ==
                                PROFILE_FIELD_BUTTON_2) {
                            msg.u.part3.button2 = profile->button[1];
                            touch_flag = 1;
                        }
                        if ((profile->fields & PROFILE_FIELD_BUTTON_3) ==
                                PROFILE_FIELD_BUTTON_3) {
                            msg.u.part3.button3 = profile->button[2];
                            touch_flag = 1;
                        }
                        if ((profile->fields & PROFILE_FIELD_BUTTON_4) ==
                                PROFILE_FIELD_BUTTON_4) {
                            msg.u.part3.button4 = profile->button[3];
                            touch_flag = 1;
                        }
                        if ((profile->fields & PROFILE_FIELD_BUTTON_5) ==
                                PROFILE_FIELD_BUTTON_5) {
                            msg.u.part3.button5 = profile->button[4];
                            touch_flag = 1;
                        }
                        if ((profile->fields & PROFILE_FIELD_BUTTON_6) ==
                                PROFILE_FIELD_BUTTON_6) {
                            msg.u.part3.button6 = profile->button[5];
                            touch_flag = 1;
                        }
                        if ((profile->fields & PROFILE_FIELD_BUTTON_7) ==
                                PROFILE_FIELD_BUTTON_7) {
                            msg.u.part3.button7 = profile->button[6];
                            touch_flag = 1;
                        }
                        if ((profile->fields & PROFILE_FIELD_BUTTON_8) ==
                                PROFILE_FIELD_BUTTON_8) {
                            msg.u.part3.button8 = profile->button[7];
                            touch_flag = 1;
                        }
                        if ((profile->fields & PROFILE_FIELD_BUTTON_9) ==
                                PROFILE_FIELD_BUTTON_9) {
                            msg.u.part3.button9 = profile->button[8];
                            touch_flag = 1;
                        }

                        if (touch_flag) {
                            msg.header.operation = XAI_MOUSE_LL_SET_PROFILE_SETTINGS;

                            if (ctx->usb_debug)
                                xai_device_packet_print(stderr, (unsigned char *)&msg, 1);

                            ret = xai_device_write_packet(ctx->dev, &msg);

                            if (ctx->usb_debug)
                                xai_device_packet_print(stderr, (unsigned char *)&msg, 0);
                        }

                    }
                }
            }
        }
    }

    return ret;
}

/*
 * Get current used profile
 * \param[in] index 0-based profile number
 */
static int xai_profile_get_current_index (struct xai_context *ctx, int *index)
{
    struct xai_ll_message msg;
    struct xai_ll_message_header hdr;
    int ret;

    hdr.null_byte = 0;
    hdr.operation = XAI_MOUSE_LL_GET_CURRENT_PROFILE;
    hdr.id = ctx->cur_id;
    hdr.part = 0;
    hdr.argument1 = 0;
    hdr.argument2 = 0;
    ret = xai_device_read_packet(ctx->dev, &hdr, &msg);

    if (ctx->usb_debug)
        xai_device_packet_print(stderr, (unsigned char *)&msg, 0);

    if (ret == RET_OK) {
        *index = msg.header.part;
        ctx->cur_id = msg.header.id;
    }
    return ret;
}

/*
 * Set current used profile
 * \param[in] index 0-based profile number
 */
static int xai_profile_set_current_index (struct xai_context *ctx, int index)
{
    struct xai_ll_message msg;
    int ret;

    memset(&msg, 0, sizeof(struct xai_ll_message));

    msg.header.operation = XAI_MOUSE_LL_SET_CURRENT_PROFILE;
    msg.header.id = ctx->cur_id;
    msg.header.part = (unsigned char)index;
    ret = xai_device_write_packet(ctx->dev, &msg);

    if (ctx->usb_debug)
        xai_device_packet_print(stderr, (unsigned char *)&msg, 0);

    return ret;
}

static int xai_profile_print (FILE *out, struct xai_profile *p, int cur_flag)
{
    int i, len;

    len = fprintf(out, "%s", p->name);

    if (cur_flag)
        fputs(" (current)\n", out);
    else
        fputc('\n', out);

    for (i = 0; i < len; i++)
        fputc('-', out);

    fprintf(out, "\nCPI1 (led off)  : %d\n"
            "CPI2 (led on)   : %d\n"
            "ExactRate (Hz)  : %d\n"
            "ExactAccel (%%)  : %d\n"
            "ExactAim  (unit): %d (0x%x)\n"
            "Free mode (unit): %d (0x%x)\n"
            "LCD brightness  : %d\n"
            "LCD contrast    : %d\n\n",
            p->cpi[0], p->cpi[1], p->rate, p->accel,
            (p->aim - 0x64)/5, p->aim,
            (p->freemove - 0x64)/5, p->freemove,
            p->lcd_brightness, p->lcd_contrast);

    for (i = 0; i < XAI_MOUSE_BUTTON_NUM; i++) {
        if (p->button[i] < (sizeof(button_setup)/sizeof(char *)))
            fprintf(out, "Button %d : %s\n", i+1, button_setup[p->button[i]]);
        else
            fprintf(out, "Button %d : 0x%x\n", i+1, p->button[i]);
    }

    fputs("\n", out);
    return RET_OK;
}


/* value match the index in 'button_setup' array */
static int button_setup_parse(const char *user_input, unsigned short *value)
{
    if (strcasecmp(user_input, "disable") == 0 ||
            (*user_input == 'd' && *(user_input+1) == '\0')) {
        *value = 13;
    } else if (strcasecmp(user_input, "left") == 0 ||
            (*user_input == 'l' && *(user_input+1) == '\0')) {
        *value = 9;
    } else if (strcasecmp(user_input, "right") == 0 ||
            (*user_input == 'r' && *(user_input+1) == '\0')) {
        *value = 10;
    } else if (strcasecmp(user_input, "middle") == 0 ||
            (*user_input == 'm' && *(user_input+1) == '\0')) {
        *value = 6;
    } else if (strcasecmp(user_input, "wheelup") == 0 ||
            (*user_input == 'u' && *(user_input+1) == 'p' &&
             *(user_input+2) == '\0')) {
        *value = 11;
    } else if (strcasecmp(user_input, "wheeldown") == 0 ||
            (*user_input == 'd' && *(user_input+1) == 'w' &&
             *(user_input+2) == '\0')) {
        *value = 12;
    } else if (strcasecmp(user_input, "tiltleft") == 0 ||
            (*user_input == 't' && *(user_input+1) == 'l' &&
             *(user_input+2) == '\0')) {
        *value = 2;
    } else if (strcasecmp(user_input, "tiltright") == 0 ||
            (*user_input == 't' && *(user_input+1) == 'r' &&
             *(user_input+2) == '\0')) {
        *value = 3;
    } else if (strcasecmp(user_input, "ieforward") == 0 ||
            (*user_input == 'f' && *(user_input+1) == 'w' &&
             *(user_input+2) == '\0')) {
        *value = 4;
    } else if (strcasecmp(user_input, "iebackward") == 0 ||
            (*user_input == 'b' && *(user_input+1) == 'w' &&
             *(user_input+2) == '\0')) {
        *value = 5;
    } else {
        return -1;
    }

    return RET_OK;
}

static int xai_profile_change_req (struct xai_profile *p, unsigned long field,
        char *arg)
{
    unsigned long n;

    /* just to know if function were called */
    p->fields |= PROFILE_FIELD_MASK;

    switch (field) {
        case PROFILE_FIELD_BUTTON_1:
            if (button_setup_parse((const char *)arg, &p->button[0])
                    == RET_OK) {
                p->fields |= field;
            } else {
                fprintf(stderr, "%s: invalid function name for button %d, ignoring option\n",
                        XAI_MOUSE_PROGRAM_NAME, 1);
            }
            break;
        case PROFILE_FIELD_BUTTON_2:
            if (button_setup_parse((const char *)arg, &p->button[1])
                    == RET_OK) {
                p->fields |= field;
            } else {
                fprintf(stderr, "%s: invalid function name for button %d, ignoring option\n",
                        XAI_MOUSE_PROGRAM_NAME, 2);
            }
            break;
        case PROFILE_FIELD_BUTTON_3:
            if (button_setup_parse((const char *)arg, &p->button[2])
                    == RET_OK) {
                p->fields |= field;
            } else {
                fprintf(stderr, "%s: invalid function name for button %d, ignoring option\n",
                        XAI_MOUSE_PROGRAM_NAME, 3);
            }
            break;
        case PROFILE_FIELD_BUTTON_4:
            if (button_setup_parse((const char *)arg, &p->button[3])
                    == RET_OK) {
                p->fields |= field;
            } else {
                fprintf(stderr, "%s: invalid function name for button %d, ignoring option\n",
                        XAI_MOUSE_PROGRAM_NAME, 4);
            }
            break;
        case PROFILE_FIELD_BUTTON_5:
            if (button_setup_parse((const char *)arg, &p->button[4])
                    == RET_OK) {
                p->fields |= field;
            } else {
                fprintf(stderr, "%s: invalid function name for button %d, ignoring option\n",
                        XAI_MOUSE_PROGRAM_NAME, 5);
            }
            break;
        case PROFILE_FIELD_BUTTON_6:
            if (button_setup_parse((const char *)arg, &p->button[5])
                    == RET_OK) {
                p->fields |= field;
            } else {
                fprintf(stderr, "%s: invalid function name for button %d, ignoring option\n",
                        XAI_MOUSE_PROGRAM_NAME, 6);
            }
            break;
        case PROFILE_FIELD_BUTTON_7:
            if (button_setup_parse((const char *)arg, &p->button[6])
                    == RET_OK) {
                p->fields |= field;
            } else {
                fprintf(stderr, "%s: invalid function name for button %d, ignoring option\n",
                        XAI_MOUSE_PROGRAM_NAME, 7);
            }
            break;
        case PROFILE_FIELD_BUTTON_8:
            if (button_setup_parse((const char *)arg, &p->button[7])
                    == RET_OK) {
                p->fields |= field;
            } else {
                fprintf(stderr, "%s: invalid function name for button %d, ignoring option\n",
                        XAI_MOUSE_PROGRAM_NAME, 8);
            }
            break;
        case PROFILE_FIELD_BUTTON_9:
            if (button_setup_parse((const char *)arg, &p->button[8])
                    == RET_OK) {
                p->fields |= field;
            } else {
                fprintf(stderr, "%s: invalid function name for button %d, ignoring option\n",
                        XAI_MOUSE_PROGRAM_NAME, 9);
            }
            break;

        case PROFILE_FIELD_CPI1:
            n = strtoul(arg, NULL, 10);
            if (n >= XAI_MOUSE_CPI_MIN && n <= XAI_MOUSE_CPI_MAX) {
                p->cpi[0] = (unsigned short)n;
                p->fields |= field;
            } else {
                fprintf(stderr, "%s: invalid value for %s, ignoring option\n",
                        XAI_MOUSE_PROGRAM_NAME, "cpi1");
            }
            break;

        case PROFILE_FIELD_CPI2:
            n = strtoul(arg, NULL, 10);
            if (n >= XAI_MOUSE_CPI_MIN && n <= XAI_MOUSE_CPI_MAX) {
                p->cpi[1] = (unsigned short)n;
                p->fields |= field;
            } else {
                fprintf(stderr, "%s: invalid value for %s, ignoring option\n",
                        XAI_MOUSE_PROGRAM_NAME, "cpi2");
            }
            break;

        case PROFILE_FIELD_RATE:
            n = strtoul(arg, NULL, 10);
            if (n >= XAI_MOUSE_RATE_MIN && n <= XAI_MOUSE_RATE_MAX) {
                p->rate = (short)n;
                p->fields |= field;
            } else {
                fprintf(stderr, "%s: invalid value for %s, ignoring option\n",
                        XAI_MOUSE_PROGRAM_NAME, "rate");
            }
            break;

        case PROFILE_FIELD_ACCEL:
            n = strtoul(arg, NULL, 10);
            if (n >= XAI_MOUSE_ACCEL_MIN && n <= XAI_MOUSE_ACCEL_MAX) {
                p->accel = (short)n;
                p->fields |= field;
            } else {
                fprintf(stderr, "%s: invalid value for %s, ignoring option\n",
                        XAI_MOUSE_PROGRAM_NAME, "accel");
            }
            break;

        case PROFILE_FIELD_FREEMOVE:
            n = strtoul(arg, NULL, 10);
            if (n >= XAI_MOUSE_FREEMOVE_MIN && n <= XAI_MOUSE_FREEMOVE_MAX) {
                p->freemove = (short)(0x64 + n * 5);
                p->fields |= field;
            } else {
                fprintf(stderr, "%s: invalid value for %s, ignoring option\n",
                        XAI_MOUSE_PROGRAM_NAME, "freemove");
            }
            break;

        case PROFILE_FIELD_AIM:
            n = strtoul(arg, NULL, 10);
            if (n >= XAI_MOUSE_AIM_MIN && n <= XAI_MOUSE_AIM_MAX) {
                p->aim = (short)(0x64 + n * 5);
                p->fields |= field;
            } else {
                fprintf(stderr, "%s: invalid value for %s, ignoring option\n",
                        XAI_MOUSE_PROGRAM_NAME, "aim");
            }
            break;

        case PROFILE_FIELD_LCD_BRIGHTNESS:
            n = strtoul(arg, NULL, 10);
            if (n >= XAI_MOUSE_LCD_BRIGHTNESS_MIN &&
                    n <= XAI_MOUSE_LCD_BRIGHTNESS_MAX) {
                p->lcd_brightness = (short)n;
                p->fields |= field;
            } else {
                fprintf(stderr, "%s: invalid value for %s, ignoring option\n",
                        XAI_MOUSE_PROGRAM_NAME, "LCD brightness");
            }
            break;

        case PROFILE_FIELD_LCD_CONTRAST:
            n = strtoul(arg, NULL, 10);

            if (n >= XAI_MOUSE_LCD_CONTRAST_MIN && n <=
                    XAI_MOUSE_LCD_CONTRAST_MAX) {
                p->lcd_contrast = (short)n;
                p->fields |= field;
            } else {
                fprintf(stderr, "%s: invalid value for %s, ignoring option\n",
                        XAI_MOUSE_PROGRAM_NAME, "LCD contrast");
            }
            break;

        default:
            return RET_ERROR_WRONG_PARAMETER;
    }

    return RET_OK;
}


static void version(void)
{
    fprintf(stdout, "%s %s\n"
            "Copyright © 2010 Free Software Foundation, Inc.\n"
            "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\n"
            "This is free software: you are free to change and redistribute it.\n"
            "There is NO WARRANTY, to the extent permitted by law.\n"
            "\n"
            "Written by Matthieu Crapet.\n",
            XAI_MOUSE_PROGRAM_NAME, XAI_MOUSE_PROGRAM_VERSION);
}

static void help(void)
{
    fprintf(stdout, "Usage: %s [options] profile_num\n"
            "\n"
            "If no option given, print human readable profile details.\n"
            "Available configuration options:\n"
            "  -c, --c1=VALUE       set CPI1 (%d - %d CPI)\n"
            "      --c2=VALUE       set CPI2 (%d - %d CPI)\n"
            "  -r, --rate=SPEED     set ExactRate (%d - %d Hz)\n"
            "  -a, --accel=PERCENT  set ExactAccel (%d - %d%%)\n"
            "  -f, --freemove=UNIT  set Freemove (%d - %d)\n"
            "      --aim=UNIT       set ExactAim (%d - %d)\n"
            "      --lcdb=N         set LCD brightness (%d - %d)\n"
            "      --lcdc=N         set LCD contrast (%d - %d)\n"
            "      --b1=ROLE        set button 1 mapping (left)\n"
            "      --b2=ROLE        set button 2 mapping (middle)\n"
            "                  ...\n"
            "      --b9=ROLE        set button 9 mapping (wheeldown)\n"
            "      --current        set as current profile\n"
            "\n"
            "Buttons: left, middle, right, iebackward, ieforward,\n"
            "         tiltleft, tiltright, wheelup, wheeldown, disable.\n"
            "\n"
            "Available global options:\n"
            "      --debug          debug mode (show usb frames data)\n"
            "      --rebind         rebind usb interface. Not done by default.\n"
            "      --version        print version of this program\n"
            "  -h, --help           show this help message and exit\n",
        XAI_MOUSE_PROGRAM_NAME,
        XAI_MOUSE_CPI_MIN, XAI_MOUSE_CPI_MAX,
        XAI_MOUSE_CPI_MIN, XAI_MOUSE_CPI_MAX,
        XAI_MOUSE_RATE_MIN, XAI_MOUSE_RATE_MAX,
        XAI_MOUSE_ACCEL_MIN, XAI_MOUSE_ACCEL_MAX,
        XAI_MOUSE_FREEMOVE_MIN, XAI_MOUSE_FREEMOVE_MAX,
        XAI_MOUSE_AIM_MIN, XAI_MOUSE_AIM_MAX,
        XAI_MOUSE_LCD_BRIGHTNESS_MIN, XAI_MOUSE_LCD_BRIGHTNESS_MAX,
        XAI_MOUSE_LCD_CONTRAST_MIN, XAI_MOUSE_LCD_CONTRAST_MAX);
}


int main(int argc, char *argv[])
{
    int c, ret, profile_number;
    static struct xai_context ctx;
    struct xai_profile newp;

    int option_index = 0;

    static struct option long_options[] =
    {
        {"debug",    no_argument, &ctx.usb_debug, 1},
        {"rebind",   no_argument, &ctx.usb_rebind, 1},
        {"current",  no_argument, &ctx.set_current_profile, 1},
        {"version",  no_argument, 0, 'v'},
        {"help",     no_argument, 0, 'h'},
        {"rate",     required_argument, 0, PROFILE_FIELD_RATE},
        {"accel",    required_argument, 0, PROFILE_FIELD_ACCEL},
        {"freemove", required_argument, 0, PROFILE_FIELD_FREEMOVE},
        {"aim",      required_argument, 0, PROFILE_FIELD_AIM},
        {"b1",       required_argument, 0, PROFILE_FIELD_BUTTON_1},
        {"b2",       required_argument, 0, PROFILE_FIELD_BUTTON_2},
        {"b3",       required_argument, 0, PROFILE_FIELD_BUTTON_3},
        {"b4",       required_argument, 0, PROFILE_FIELD_BUTTON_4},
        {"b5",       required_argument, 0, PROFILE_FIELD_BUTTON_5},
        {"b6",       required_argument, 0, PROFILE_FIELD_BUTTON_6},
        {"b7",       required_argument, 0, PROFILE_FIELD_BUTTON_7},
        {"b8",       required_argument, 0, PROFILE_FIELD_BUTTON_8},
        {"b9",       required_argument, 0, PROFILE_FIELD_BUTTON_9},
        {"c1",       required_argument, 0, PROFILE_FIELD_CPI1},
        {"c2",       required_argument, 0, PROFILE_FIELD_CPI2},
        { "lcdb",    required_argument, 0, PROFILE_FIELD_LCD_BRIGHTNESS},
        { "lcdc",    required_argument, 0, PROFILE_FIELD_LCD_CONTRAST},
        {0, 0, 0, 0}
    };

    if (argc == 1) {
        fprintf(stderr, "%s: missing arguments\nTry `%s --help' for more information.\n",
                XAI_MOUSE_PROGRAM_NAME, XAI_MOUSE_PROGRAM_NAME);
        return -1;
    }

    memset(&newp, 0, sizeof(struct xai_profile));

    while ((c = getopt_long(argc, argv, "f:c:r:a:hv", long_options,
                    &option_index)) != -1) {
        switch (c & PROFILE_FIELD_MASK) {
            case 0:
                break;

            case (PROFILE_FIELD_MASK ^ 0xFF):
                ret = xai_profile_change_req(&newp, (unsigned long)c, optarg);
                break;
            case 'c':
                ret = xai_profile_change_req(&newp, PROFILE_FIELD_CPI1, optarg);
                break;
            case 'r':
                ret = xai_profile_change_req(&newp, PROFILE_FIELD_RATE, optarg);
                break;
            case 'a':
                ret = xai_profile_change_req(&newp, PROFILE_FIELD_ACCEL, optarg);
                break;
            case 'f':
                ret = xai_profile_change_req(&newp, PROFILE_FIELD_FREEMOVE, optarg);
                break;

            case 'h':
                help();
                return 0;
            case 'v':
                version();
                return 0;

            default:
                fprintf(stderr, "%s: bad option (%c)\nTry `%s --help' for more information.\n",
                        XAI_MOUSE_PROGRAM_NAME, c, XAI_MOUSE_PROGRAM_NAME);
                return -1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "%s: missing profile number\n", XAI_MOUSE_PROGRAM_NAME);
        return -1;
    }

    profile_number = atoi(argv[optind]);

    if (profile_number <= 0 || profile_number > XAI_MOUSE_PROFILE_NUM) {
        fprintf(stderr, "%s: invalid profile number. Must be from 1 to %d.\n",
                XAI_MOUSE_PROGRAM_NAME, XAI_MOUSE_PROFILE_NUM);
        return -1;
    }
    profile_number--;

    if ((ret = xai_init(XAI_MOUSE_VENDOR_ID, XAI_MOUSE_PRODUCT_ID,
                    XAI_MOUSE_INTERFACE_NUM, &ctx)) != RET_OK) {
        fprintf(stderr, "%s: error in xai_init (%d)\n",
                XAI_MOUSE_PROGRAM_NAME, ret);
        return -1;
    }

    if ((ret = xai_device_init(&ctx)) != RET_OK) {
        fprintf(stderr, "%s: error in xai_device_init (%d)\n",
                XAI_MOUSE_PROGRAM_NAME, ret);
        xai_uninit(&ctx);
        return -2;
    }

    if ((newp.fields != 0) || (ctx.set_current_profile)) {
        ret = xai_profile_set_config (&ctx, profile_number, &newp);
        if (ret == RET_OK) {

            /* if current changeset apply to current profile, reload it */
            if ((profile_number == ctx.cur_index) || (ctx.set_current_profile)) {
                ret = xai_profile_set_current_index(&ctx, profile_number);
                if (ret != RET_OK)
                    fprintf(stderr, "%s: error in xai_profile_set_current_index (%d)\n",
                            XAI_MOUSE_PROGRAM_NAME, ret);
            }

            ret = xai_device_write_to_flash(&ctx);
            if (ret != RET_OK)
                fprintf(stderr, "%s: error in xai_device_write_to_flash (%d)\n",
                        XAI_MOUSE_PROGRAM_NAME, ret);

        } else {
            fprintf(stderr, "%s: error in xai_profile_set_config (%d)\n",
                    XAI_MOUSE_PROGRAM_NAME, ret);
        }

    } else {
        xai_profile_print(stdout, &ctx.p[profile_number],
                profile_number == ctx.cur_index);
    }

    xai_uninit(&ctx);

    return 0;
}

// vim: filetype=c enc=latin1 shiftwidth=4 tabstop=4 expandtab
