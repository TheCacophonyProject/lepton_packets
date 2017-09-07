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

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static void pabort(const char *s)
{
    perror(s);
    abort();
}

static const char *device = "/dev/spidev0.0";
static uint8_t mode = 3;
static uint8_t bits = 8;
static uint32_t speed = 16000000;
static uint16_t delay;

#define VOSPI_PACKET_SIZE (164)
#define PACKETS_PER_READ (120)
#define TRANSFER_SIZE (VOSPI_PACKET_SIZE * PACKETS_PER_READ)

#define STATE_VALID (0)
#define STATE_INVALID (1)
#define STATE_INVALID_PKTNUM (2)

static int invalid = 0;
static int invalid_pktnum = 0;
static int last_packet_number = -1;
static int state = STATE_INVALID;
static int last_state = -1;


void transfer(int fd)
{
    int ret;
    int i;
    unsigned int header;
    int packet_number;
    uint8_t tx[TRANSFER_SIZE] = {0, };
    uint8_t rx[TRANSFER_SIZE] = {0, };
    uint8_t *packet;
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
    
	if ((header & 0x0F00) == 0x0F00) {
	    state = STATE_INVALID;
	    invalid++;
	} else {
	    packet_number = header & 0x0FFF;
	    if (packet_number > 62) {
		state = STATE_INVALID_PKTNUM;
		invalid_pktnum++;
	    } else {
		state = STATE_VALID;
		if (packet_number == 20) {
		    printf("p 20: %02x\n", packet[0]);
		}
	    }
	}

	if (state != last_state) {
	    switch (last_state) {
	    case STATE_VALID:
		printf("saw packets: 0-%d\n", last_packet_number);
		last_packet_number = -1;
		break;
	    case STATE_INVALID:
		printf("invalid: %d\n", invalid);
		invalid = 0;
		break;
	    case STATE_INVALID_PKTNUM:
		printf("invalid packet numbers: %d\n", invalid_pktnum);
		invalid_pktnum = 0;
		break;
	    }
	    last_state = state;
	}

	if (state == STATE_VALID) {
	    if (packet_number != last_packet_number+1) {
		if (last_packet_number != -1) {
		    printf("saw packets: 0-%d\n", last_packet_number);
		}
	    }
	    last_packet_number = packet_number;
	}
    }
}

int main(int argc, char *argv[])
{
    int ret = 0;
    int fd;
    int i;


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

    printf("spi mode: %d\n", mode);
    printf("bits per word: %d\n", bits);
    printf("max speed: %d Hz (%d KHz)\n", speed, speed/1000);
    printf("size per read: %d\n", TRANSFER_SIZE);

    /* /\* XXX force a reset? *\/ */
    /* sleep(1); */

    for (i = 0; i < 5000; i++) {
        transfer(fd);
    }

    close(fd);

    return ret;
}