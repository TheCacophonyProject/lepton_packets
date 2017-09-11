/*
Copyright (c) 2014, Pure Engineering LLC
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <limits.h>
#include <time.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

// XXX timestamp outputs to get a better idea of what's happening

static void pabort(const char *s)
{
    perror(s);
    abort();
}

static const char *device = "/dev/spidev0.0";
static uint8_t mode = 3;
static uint8_t bits = 8; // XXX get the driver to do the work by setting to 16
static uint32_t speed = 16000000;
static uint16_t delay;

#define VOSPI_PACKET_SIZE (164)
// XXX play around with this
#define PACKETS_PER_READ (120)
#define TRANSFER_SIZE (VOSPI_PACKET_SIZE * PACKETS_PER_READ)

#define STATE_VALID (0)
#define STATE_INVALID (1)
#define STATE_INVALID_PKTNUM (2)

static int invalid = 0;
static int invalid_pktnum = 0;
static int last_pkt_num = -1;
static int state = STATE_INVALID;
static int last_state = -1;

static int transfer_count = 0;
static int last_good_count = 0;

static uint8_t tx[TRANSFER_SIZE] = {0, };

void debug(char *fmt, ...) {
    time_t t = time(NULL);
    struct tm *ti = localtime(&t);
    va_list ap;
    va_start(ap, fmt);
    printf("%02d:%02d:%02d: ", ti->tm_hour, ti->tm_min, ti->tm_sec);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

int is_valid_seq(int prev, int now) {
    if (now == 0 && prev == 60) {
        return 1;
    }
    if (now == prev + 1) {
        return 1;
    }
    return 0;
}

int transfer(int fd)
{
    int ret;
    int i;
    unsigned int header;
    int pkt_num;
    uint8_t rx[TRANSFER_SIZE] = {0, };
    uint8_t *packet;

    transfer_count++;

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = TRANSFER_SIZE,
        .delay_usecs = delay,
        .speed_hz = speed,
        .bits_per_word = bits,
    };

    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 1) {
        pabort("can't send spi message");
    }

    for (i = 0; i < PACKETS_PER_READ; i++) {
        packet = &rx[i * VOSPI_PACKET_SIZE];
        header = packet[1] | packet[0] << 8;

        /* XXX if first bit of buffer is set, trigger a reset */

        if ((header & 0x0F00) == 0x0F00) {
            state = STATE_INVALID;
            invalid++;
        } else {
            pkt_num = header & 0x0FFF;
            if (pkt_num > 60) {
                debug("invalid packet number: %d", pkt_num);
                state = STATE_INVALID_PKTNUM;
                invalid_pktnum++;
                return 1;
            } else {
                state = STATE_VALID;
                if (pkt_num == 20) {
                    debug("p %d: %d", pkt_num, packet[0] >> 4);
                }
            }
        }

        if (state != last_state) {
            switch (last_state) {
            case STATE_VALID:
                debug("saw packets: 0-%d", last_pkt_num);
                last_pkt_num = -1;
                break;
            case STATE_INVALID:
                debug("invalid: %d", invalid);
                invalid = 0;
                break;
            case STATE_INVALID_PKTNUM:
                debug("invalid packet numbers: %d", invalid_pktnum);
                invalid_pktnum = 0;
                break;
            }
            last_state = state;
        }

        if (state == STATE_VALID) {
            if (pkt_num == 0 && last_pkt_num == 0) {
                // XXX end of read?
                debug("saw pkt 0 twice (%02x %02x %02x %02x)", packet[0], packet[1], packet[2], packet[3]);
                return 0;
            }
            if (!is_valid_seq(last_pkt_num, pkt_num)) {
                if (last_pkt_num != -1) {
                    debug("saw packets: 0-%d", last_pkt_num);
                }
                debug("pkts out of sequence: %d, previous %d", pkt_num, last_pkt_num);
                return 1;
            }
            last_pkt_num = pkt_num;
        }
    }

    return 0;
}


int spi_init() {
    int fd, ret;
    fd = open(device, O_RDWR);
    if (fd < 0)
    {
        pabort("can't open device");
    }

    ret = ioctl(fd, SPI_IOC_WR_MODE, &mode);
    if (ret == -1)
    {
        pabort("can't set spi mode");
    }

    ret = ioctl(fd, SPI_IOC_RD_MODE, &mode);
    if (ret == -1)
    {
        pabort("can't get spi mode");
    }

    ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    if (ret == -1)
    {
        pabort("can't set bits per word");
    }

    ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits);
    if (ret == -1)
    {
        pabort("can't get bits per word");
    }

    ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    if (ret == -1)
    {
        pabort("can't set max speed hz");
    }

    ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
    if (ret == -1)
    {
        pabort("can't get max speed hz");
    }

    debug("spi mode: %d", mode);
    debug("bits per word: %d", bits);
    debug("max speed: %d Hz (%d KHz)", speed, speed/1000);
    debug("size per read: %d", TRANSFER_SIZE);

    return fd;
}

int reset(int fd) {
    debug("reset!");
    close(fd);

    invalid = 0;
    invalid_pktnum = 0;
    last_pkt_num = -1;
    state = STATE_INVALID;
    last_state = -1;
    transfer_count = 0;
    last_good_count = 0;

    usleep(200 * 1000); // 200ms
    return spi_init();
}

int main(int argc, char *argv[])
{
    int fd = spi_init();
    int need_reset;

    while (1 == 1) {
        need_reset = transfer(fd);
        if (need_reset) {
            reset(fd);
        }
    }

    close(fd);
    return 0;
}
