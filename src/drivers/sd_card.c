//
// Created by Artur Twardzik on 09/04/2026.
//

#include "sd_card.h"

#include "errno.h"
#include "gpio.h"
#include "spi.h"
#include "time.h"
#include "tty.h"
#include "kernel/memory.h"

static constexpr int SD_CS = 13;
static constexpr int SD_SPI_BLOCK = 1;

static constexpr uint8_t CMD0 = 0;
static constexpr uint8_t CMD8 = 8;
static constexpr uint8_t CMD17 = 17;
static constexpr uint8_t CMD24 = 24;
static constexpr uint8_t CMD55 = 55;
static constexpr uint8_t CMD58 = 58;
static constexpr uint8_t ACMD41 = 41;


static bool sd_active = false;


static void sd_write_cmd(uint8_t cmd, uint32_t argument, uint8_t crc) {
        spi_tx(SD_SPI_BLOCK, cmd | 0x40);

        spi_tx(SD_SPI_BLOCK, (argument >> 24) & 0xff);
        spi_tx(SD_SPI_BLOCK, (argument >> 16) & 0xff);
        spi_tx(SD_SPI_BLOCK, (argument >> 8) & 0xff);
        spi_tx(SD_SPI_BLOCK, argument & 0xff);

        spi_tx(SD_SPI_BLOCK, crc | 0x01);
}

static uint8_t sd_read_res1(void) {
        uint8_t i = 0;
        uint8_t res1;

        // keep polling until actual data received
        while ((res1 = spi_rx(SD_SPI_BLOCK)) == 0xff) {
                i++;

                // if no data received for 8 bytes, break
                if (i > 8) break;
        }

        return res1;
}


static int sd_card_read512_block(const uint32_t block_number, char *buffer) {
        if (!sd_active) {
                return -ENODEV;
        }

        spi_tx(SD_SPI_BLOCK, 0xff);
        clr_pin(SD_CS);

        sd_write_cmd(CMD17, block_number, 0);
        if (sd_read_res1() == 0xff) {
                return -1; //not read
        }

        while (spi_rx(SD_SPI_BLOCK) != 0xfe) {}

        for (int i = 0; i < 512; ++i) {
                buffer[i] = spi_rx(SD_SPI_BLOCK);
        }

        //ignore crc
        spi_rx(SD_SPI_BLOCK);
        spi_rx(SD_SPI_BLOCK);

        set_pin(SD_CS);
        return 0;
}

static int sd_card_write512_block(uint32_t block_number, char *buffer) {
        if (!sd_active) {
                return -ENODEV;
        }

        spi_tx(SD_SPI_BLOCK, 0xff);
        clr_pin(SD_CS);

        sd_write_cmd(CMD24, block_number, 0);
        if (sd_read_res1() == 0xff) {
                return -1; //not read
        }

        spi_tx(SD_SPI_BLOCK, 0xfe); //data start token

        for (int i = 0; i < 512; ++i) {
                spi_tx(SD_SPI_BLOCK, buffer[i]);
        }

        uint8_t res;
        while ((res = spi_rx(SD_SPI_BLOCK)) == 0xff) {}

        if ((res & 0x1f) == 0x05) {
                //data accepted
                while ((res = spi_rx(SD_SPI_BLOCK)) == 0x00) {}
        }

        spi_tx(SD_SPI_BLOCK, 0xff);
        set_pin(SD_CS);
        spi_tx(SD_SPI_BLOCK, 0xff);

        return 0;
}

static int sd_card_read(const uint32_t block_number, const size_t block_size, char *buffer) {
        if (block_size < 512 || block_size % 512 != 0) {
                return -1;
        }

        if (block_size == 512) {
                return sd_card_read512_block(block_number, buffer);
        }

        for (size_t i = 0; i < block_size / 512; ++i) {
                char *bufptr = buffer + (i * 512);

                if (sd_card_read512_block(block_number + i, bufptr) != 0) {
                        return -1;
                }
        }

        return 0;
}

static int sd_card_write(uint32_t block_number, size_t block_size, char *buffer) {
        if (block_size < 512 || block_size % 512 != 0) {
                return -1;
        }

        if (block_size == 512) {
                return sd_card_write512_block(block_number, buffer);
        }

        for (size_t i = 0; i < block_size / 512; ++i) {
                char *bufptr = buffer + (i * 512);

                if (sd_card_write512_block(block_number + i, bufptr) != 0) {
                        return -1;
                }
        }

        return 0;
}

//FIXME: deassert CS on failure
struct HardDriveOperations *init_sd_card(void) {
        printk_status_init("Initializing SD card");
        spi_set_baudrate(SD_SPI_BLOCK, 2, 3);

        printk_status_step();

        // Power up
        set_pin(SD_CS);
        for (int i = 0; i < 10; ++i) {
                spi_tx(1, 0xff);
                delay_us(10); //pretend to be much slower
        }
        printk_status_step();

        // Reset into SPI mode
        clr_pin(SD_CS);
        spi_tx(SD_SPI_BLOCK, 0xff);
        sd_write_cmd(CMD0, 0, 0x95);
        int i = 0;
        while (spi_rx(SD_SPI_BLOCK) != 0x01) {
                if (++i > 30) {
                        goto error_ret; //timeout
                }
        }
        printk_status_step();

        // v2 card
        sd_write_cmd(CMD8, 0x000001AA, 0x86);
        i = 0;
        uint8_t r1 = 0xff;
        while ((r1 = spi_rx(SD_SPI_BLOCK)) == 0xff) {
                if (++i > 30) {
                        goto error_ret; //timeout
                }
        }
        if (r1 != 0x01) {
                printk_status_finish(-1);
                goto error_ret; //not a valid v2 card
        }

        uint8_t r7[4];
        for (int j = 0; j < 4; ++j) r7[j] = spi_rx(SD_SPI_BLOCK);
        if (r7[2] != 0x01 || r7[3] != 0xAA) {
                printk_status_finish(-1);
                goto error_ret; //bad echo
        }
        printk_status_step();

        //
        i = 0;
        while (1) {
                spi_tx(SD_SPI_BLOCK, 0xff);
                sd_write_cmd(CMD55, 0, 0x65);
                int k = 0;
                while (spi_rx(SD_SPI_BLOCK) == 0xff) {
                        if (++k > 30) {
                                printk_status_finish(-1);
                                goto error_ret; //timeout
                        }
                }

                spi_tx(SD_SPI_BLOCK, 0xff);
                sd_write_cmd(ACMD41, 0x40000000, 0x77);
                k = 0;
                uint8_t acmd_r1 = 0xff;
                while ((acmd_r1 = spi_rx(SD_SPI_BLOCK)) == 0xff) {
                        if (++k > 30) {
                                printk_status_finish(-1);
                                goto error_ret; //timeout
                        }
                }

                if (acmd_r1 == 0x00) break; // card initialized
                if (acmd_r1 != 0x01) {
                        goto error_ret;
                }

                if (++i > 100) {
                        printk_status_finish(-1);
                        goto error_ret; //timeout
                }
                delay_ms(10);
        }
        printk_status_step();

        // read OCR, check CCS bit to confirm SDHC/SDXC
        spi_tx(SD_SPI_BLOCK, 0xff);
        sd_write_cmd(CMD58, 0, 0xFD);

        i = 0;
        while (spi_rx(SD_SPI_BLOCK) != 0x00) {
                if (++i > 30) {
                        printk_status_finish(-1);
                        goto error_ret; //timeout
                }
        }

        uint8_t ocr[4];
        for (int j = 0; j < 4; ++j) ocr[j] = spi_rx(SD_SPI_BLOCK);

        if (!(ocr[0] & 0x80)) {
                printk_status_finish(-1);
                goto error_ret; // power not up yet, shouldn't happen
        }

        bool is_sdhc = (ocr[0] & 0x40) != 0;

        set_pin(SD_CS);

        struct HardDriveOperations *hd_op = kmalloc(sizeof(*hd_op));
        hd_op->read_block = sd_card_read;
        hd_op->write_block = sd_card_write;
        hd_op->deinit = nullptr;

        sd_active = true;
        spi_set_baudrate(SD_SPI_BLOCK, 2, 3); // 150[MHz] / 2(1+3) = 18.75[MHz]
        printk_status_finish(0);

        return hd_op;

error_ret:
        set_pin(SD_CS);
        printk_status_finish(-1);
        return nullptr;
}
