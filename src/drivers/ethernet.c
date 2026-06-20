//
// Created by Artur Twardzik on 30/03/2026.
//

// This is a "local" driver, meaning the interface is connected exactly to SPI0 pins 16-19
#include "ethernet.h"

#include "errno.h"
#include "gpio.h"
#include "spi.h"
#include "time.h"
#include "fs/file.h"
#include "kernel/error.h"
#include "kernel/isr.h"
#include "kernel/memory.h"
#include "kernel/mutex.h"

static constexpr int ETH_CS_PIN = 17;
static constexpr int ETH_INT_PIN = 21;
static constexpr int ETH_SPI_BLOCK = 0;
static constexpr uint16_t WIZNET_SOCKET_COUNT = 4;
static constexpr uint16_t S0_TX_BASE = 0x4000;
static constexpr uint16_t S0_RX_BASE = 0x6000;

static constexpr uint16_t MR = 0x0000;
static constexpr uint16_t GAR = 0x0001;
static constexpr uint16_t SUBR = 0x0005;
static constexpr uint16_t SHAR = 0x0009;
static constexpr uint16_t SIPR = 0x000f;
static constexpr uint16_t IMR = 0x0016;
static constexpr uint16_t RTR = 0x0017;
static constexpr uint16_t RCR = 0x0019;
static constexpr uint16_t VERR_REG = 0x0080;

#define SN_MR(socket) (0x0000 + 0x0100 * ((uint16_t) socket + 4))
#define SN_CR(socket) (0x0001 + 0x0100 * ((uint16_t) socket + 4))
#define SN_IR(socket) (0x0002 + 0x0100 * ((uint16_t) socket + 4))
#define SN_SR(socket) (0x0003 + 0x0100 * ((uint16_t) socket + 4))
#define SN_PORTR(socket) (0x0004 + 0x0100 * ((uint16_t) socket + 4))
#define SN_DHAR(socket) (0x0006 + 0x0100 * ((uint16_t) socket + 4))
#define SN_DIPR(socket) (0x000c + 0x0100 * ((uint16_t) socket + 4))
#define SN_DPORTR(socket) (0x0010 + 0x0100 * ((uint16_t) socket + 4))
#define SN_RXBUF_SIZE(socket) (0x001e + 0x0100 * ((uint16_t) socket + 4))
#define SN_TXBUF_SIZE(socket) (0x001f + 0x0100 * ((uint16_t) socket + 4))
#define SN_TX_FSR(socket) (0x0020 + 0x0100 * ((uint16_t) socket + 4))
#define SN_TX_RD(socket) (0x0022 + 0x0100 * ((uint16_t) socket + 4))
#define SN_TX_WR(socket) (0x0024 + 0x0100 * ((uint16_t) socket + 4))
#define SN_RX_RSR(socket) (0x0026 + 0x0100 * ((uint16_t) socket + 4))
#define SN_RX_RD(socket) (0x0028 + 0x0100 * ((uint16_t) socket + 4))
#define SN_RX_WR(socket) (0x002a + 0x0100 * ((uint16_t) socket + 4))

enum SocketInterrupt {
        INT_SENDOK  = 0x16,
        INT_TIMEOUT = 0x08,
        INT_RECV    = 0x04,
        INT_DISCON  = 0x02,
        INT_CON     = 0x01,
};

enum SocketStatus {
        SOCK_CLOSED      = 0x00,
        SOCK_INIT        = 0x13, /*socket opened as TCP mode*/
        SOCK_LISTEN      = 0x14, /*socket is TCP mode and wait for peer connection*/
        SOCK_ESTABLISHED = 0x17, /*socket is TCP mode and TCP connection is done*/
        SOCK_CLOSE_WAIT  = 0x1c, /*socket is TCP mode and received disconnection request*/
        SOCK_UDP         = 0x22, /*socket opened as UDP mode*/
        SOCK_IPRAW       = 0x32, /*socket opened as IPRAW mode*/
        SOCK_MACRAW      = 0x42, /*socket opened as MACRAW mode*/
};

enum SocketCommand {
        CMD_OPEN      = 0x01,
        CMD_LISTEN    = 0x02,
        CMD_CONNECT   = 0x04,
        CMD_DISCON    = 0x08,
        CMD_CLOSE     = 0x10,
        CMD_SEND      = 0x20,
        CMD_SEND_MAC  = 0x21,
        CMD_SEND_KEEP = 0x22,
        CMD_RECV      = 0x40,
};

struct WIZnetSocket {
        struct Socket socket;

        uint16_t txbuf_start;
        size_t txbuf_size_max;
        unsigned int txbuf_mask;

        uint16_t rxbuf_start;
        size_t rxbuf_size_max;
        unsigned int rxbuf_mask;

        unsigned int index;

        bool taken;
};

static struct {
        struct NetworkInterface interface;

        struct Process *socket_waiting_processes[4];
        uint8_t socket_interrupts[4];

        struct WIZnetSocket *sockets[WIZNET_SOCKET_COUNT];
} wiznet_network_interface;

void eth_irq(void);

/**
 * Sets up SPI interface for communication with WIZnet W5100S
 */
static void setup_eth_gpio(void) {
        //spi0 rx
        GPIO_function_select(16, 1);
        output_enable_pin(16);

        //spi0 sck
        GPIO_function_select(18, 1);
        output_enable_pin(18);

        //spi0 tx
        GPIO_function_select(19, 1);
        output_enable_pin(19);

        //manual CS
        init_pin_output(ETH_CS_PIN);

        spi_init(0, 2, 15, 0);

        init_pin_input_with_pull(ETH_INT_PIN, true);
        set_irq_pin_enabled_edge_low(ETH_INT_PIN);
        set_isr(IO_IRQ_BANK0, eth_irq); //FIXME: implement global pin interrupt handler and determine pin inside
}


static mutex_t eth_spi_mutex;
static mutex_t eth_transaction_mutex;
static mutex_t eth_command_send_mutex;

/**
 * Writes to wiznet register / memory
 * @param reg_offset specified hw register / memory offset
 * @param data array of n bytes to be sent
 * @param len length of the bytes buffer
 */
static void eth_write_mem(uint16_t reg_offset, const uint8_t *data, const size_t len) {
        mutex_lock(&eth_spi_mutex);

        for (size_t i = 0; i < len; i++) {
                clr_pin(ETH_CS_PIN);

                spi_tx(ETH_SPI_BLOCK, 0xf0);
                spi_tx(ETH_SPI_BLOCK, reg_offset >> 8);
                spi_tx(ETH_SPI_BLOCK, reg_offset & 0x00ff);

                spi_tx(ETH_SPI_BLOCK, data[i]);

                reg_offset += 1;
                set_pin(ETH_CS_PIN);
        }

        mutex_unlock(&eth_spi_mutex);
}

/**
 * Reads from wiznet register / memory
 * @param reg_offset specified hw resiter / memory offset
 * @param buf buffer where the data will be written into
 * @param len length of data to be read
 */
static void eth_read_mem(uint16_t reg_offset, uint8_t *buf, const size_t len) {
        mutex_lock(&eth_spi_mutex);

        for (size_t i = 0; i < len; i++) {
                clr_pin(ETH_CS_PIN);
                spi_tx(ETH_SPI_BLOCK, 0x0f);

                spi_tx(ETH_SPI_BLOCK, reg_offset >> 8);
                spi_tx(ETH_SPI_BLOCK, reg_offset & 0x00ff);

                buf[i] = spi_rx(ETH_SPI_BLOCK);

                reg_offset += 1;
                set_pin(ETH_CS_PIN);
        }

        mutex_unlock(&eth_spi_mutex);
}

void eth_irq() {
        clr_interrupt(ETH_INT_PIN);

        uint8_t irq[4];
        eth_read_mem(SN_IR(0), irq, 1);
        eth_read_mem(SN_IR(1), irq + 1, 1);
        eth_read_mem(SN_IR(2), irq + 2, 1);
        eth_read_mem(SN_IR(3), irq + 3, 1);

        for (size_t i = 0; i < 4; ++i) {
                if (irq[i] & INT_CON) {
                        wiznet_network_interface.socket_interrupts[i] |= INT_CON;
                        wiznet_network_interface.socket_waiting_processes[i]->pstate = READY;
                        //what if it does not exist?
                        break;
                }
        }

        for (size_t i = 0; i < 4; ++i) {
                if (irq[i] & INT_TIMEOUT) {
                        wiznet_network_interface.socket_interrupts[i] |= INT_TIMEOUT;
                        wiznet_network_interface.socket_waiting_processes[i]->pstate = READY;
                        break;
                }
        }

        for (size_t i = 0; i < 4; ++i) {
                if (irq[i] & INT_RECV) {
                        wiznet_network_interface.socket_interrupts[i] |= INT_RECV;
                        wiznet_network_interface.socket_waiting_processes[i]->pstate = READY;
                        break;
                }
        }

        for (size_t i = 0; i < 4; ++i) {
                if (irq[i] & INT_DISCON) {
                        wiznet_network_interface.socket_interrupts[i] |= INT_DISCON;
                        wiznet_network_interface.socket_waiting_processes[i]->pstate = READY;
                        break;
                }
        }

        for (size_t i = 0; i < 4; ++i) {
                //clear all interrupts
                irq[0] = INT_SENDOK | INT_TIMEOUT | INT_RECV | INT_DISCON | INT_CON;
                eth_write_mem(SN_IR(i), irq, 1);
        }
}


static void socket_command(const int socket_number, const enum SocketCommand command) {
        mutex_lock(&eth_command_send_mutex);
        const uint8_t send[] = {command};
        eth_write_mem(SN_CR(socket_number), send, 1);

        // Wait until command is cleared
        uint8_t cr[1];
        do {
                eth_read_mem(SN_CR(socket_number), cr, 1);
        } while (cr[0] != 0x00);

        mutex_unlock(&eth_command_send_mutex);
}

/**
 * Tries to open socket in specified mode
 * @param sock socket to be opened
 * @param mode specified socket mode
 * @return socket number on success; -ESOCKTNOSUPPORT on failure to open socket in specified mode
 */
static int open_socket(struct Socket *sock, const enum SocketMode mode) {
        mutex_lock(&eth_transaction_mutex);
        struct WIZnetSocket *socket = (struct WIZnetSocket *) sock;

        if (mode == MACRAW && socket->index != 0) {
                mutex_unlock(&eth_transaction_mutex);
                return -ESOCKTNOSUPPORT;
        }

open:
        const uint8_t socket_mode[] = {mode};
        eth_write_mem(SN_MR(socket->index), socket_mode, 1);

        socket_command(socket->index, CMD_OPEN);

        uint8_t sr[1];
        eth_read_mem(SN_SR(socket->index), sr, 1);
        const enum SocketStatus status = sr[0];
        if (!((status == SOCK_INIT && mode == TCP) || (status == SOCK_MACRAW && mode == MACRAW))) {
                goto open;
        }

        socket->socket.mode = mode;
        mutex_unlock(&eth_transaction_mutex);
        return 0;
}

static int bind_socket(struct Socket *sock, const uint16_t port) {
        struct WIZnetSocket *socket = (struct WIZnetSocket *) sock;

        const uint8_t portr[] = {((char *) &port)[0], ((char *) &port)[1]};
        eth_write_mem(SN_PORTR(socket->index), portr, 2);

        socket->socket.port = port;
        return 0;
}

static int listen_socket(struct Socket *sock) {
        const struct WIZnetSocket *socket = (struct WIZnetSocket *) sock;

        socket_command(socket->index, CMD_LISTEN);

        uint8_t sr[1];
        eth_read_mem(SN_SR(socket->index), sr, 1);
        const enum SocketStatus status = sr[0];
        if (status != SOCK_LISTEN) {
                return -ENOTCONN; //maybe better code?
        }

        return 0;
}

static int accept_socket(struct Socket *sock, struct sockaddr *addr, size_t addrlen) {
        const struct WIZnetSocket *socket = (struct WIZnetSocket *) sock;

        uint8_t con[1];
        eth_read_mem(SN_IR(socket->index), con, 1);
        if ((con[0] & INT_CON) == 0) {
                struct Process *current_process = scheduler_get_current_process();
                current_process->pstate = WAITING_FOR_RESOURCE;
                wiznet_network_interface.socket_waiting_processes[socket->index] = current_process;
                save_kernelmode_and_context_switch();
        }


        eth_read_mem(SN_DIPR(socket->index), addr->sa_data + 2, 4);
        eth_read_mem(SN_DPORTR(socket->index), addr->sa_data, 2);

        return 0;
}

static int connect_socket(struct Socket *sock, struct sockaddr *addr, size_t addrlen) {
        const struct WIZnetSocket *socket = (struct WIZnetSocket *) sock;

        eth_write_mem(SN_DIPR(socket->index), addr->sa_data + 2, 4);
        eth_write_mem(SN_DPORTR(socket->index), addr->sa_data, 2);

        socket_command(socket->index, CMD_CONNECT);

        uint8_t con[1];
        eth_read_mem(SN_IR(socket->index), con, 1);
        if ((con[0] & INT_CON) == 0) {
                struct Process *current_process = scheduler_get_current_process();
                current_process->pstate = WAITING_FOR_RESOURCE;
                wiznet_network_interface.socket_waiting_processes[socket->index] = current_process;
                save_kernelmode_and_context_switch();
        }

        if (wiznet_network_interface.socket_interrupts[socket->index] & INT_TIMEOUT) {
                wiznet_network_interface.socket_interrupts[socket->index] ^= INT_TIMEOUT;
                return -ETIMEDOUT;
        }

        if (wiznet_network_interface.socket_interrupts[socket->index] & INT_CON) {
                wiznet_network_interface.socket_interrupts[socket->index] ^= INT_CON;
                return 0;
        }

        return -ECONNABORTED;
}

static int active_close(struct File *socket_file) {
        mutex_lock(&eth_transaction_mutex);
        struct WIZnetSocket *socket = (struct WIZnetSocket *) socket_file;
        int ret = 0;

        uint8_t sr[1];
        eth_read_mem(SN_SR(socket->index), sr, 1);
        enum SocketStatus status = sr[0];
        if (status == SOCK_CLOSED) {
                goto socket_close_ret;
        }

        if (socket->index != 0) {
                socket_command(socket->index, CMD_DISCON);

                uint8_t con[1];
                eth_read_mem(SN_IR(socket->index), con, 1);
                if ((con[0] & INT_CON) == 0) {
                        struct Process *current_process = scheduler_get_current_process();
                        current_process->pstate = WAITING_FOR_RESOURCE;
                        wiznet_network_interface.socket_waiting_processes[socket->index] = current_process;
                        save_kernelmode_and_context_switch();
                }

                if (wiznet_network_interface.socket_interrupts[socket->index] & INT_TIMEOUT) {
                        wiznet_network_interface.socket_interrupts[socket->index] ^= INT_TIMEOUT;
                        ret = -ETIMEDOUT;
                }
        }

        socket_command(socket->index, CMD_CLOSE);

        do {
                uint8_t sr[1];
                eth_read_mem(SN_SR(socket->index), sr, 1);
                status = sr[0];
        } while (status != SOCK_CLOSED);

socket_close_ret:
        socket->taken = false;
        mutex_unlock(&eth_transaction_mutex);
        return ret;
}


static int wait_send_complete(const struct WIZnetSocket *socket) {
        mutex_lock(&eth_transaction_mutex);
        uint8_t ir[1];
        do {
                eth_read_mem(SN_IR(socket->index), ir, 1);

                ir[0] &= INT_SENDOK | INT_TIMEOUT;
        } while (!ir[0]);

        if (ir[0] & INT_SENDOK) {
                constexpr uint8_t clear[] = {INT_SENDOK};
                eth_write_mem(SN_IR(socket->index), clear, 1);
        }
        else {
                mutex_unlock(&eth_transaction_mutex);
                return -ETIMEDOUT;
        }

        mutex_unlock(&eth_transaction_mutex);
        return 0;
}

static void add_to_socket_pointer_reg(struct WIZnetSocket *socket, uint16_t reg_offset, const size_t appended_size) {
        uint8_t offsets[2];
        eth_read_mem(reg_offset, offsets, 2);
        const uint16_t new_offset = ((offsets[0] << 8) | (offsets[1])) + appended_size;

        const uint8_t ptr[] = {new_offset >> 8, new_offset & 0xff};
        eth_write_mem(reg_offset, ptr, 2);
}

static bool fits_socket_txbuf(const struct WIZnetSocket *socket, const size_t frame_size) {
        uint8_t free_space[2];
        eth_read_mem(SN_TX_FSR(socket->index), free_space, 2);
        const uint16_t current_free_space = (free_space[0] << 8) | (free_space[1]);
        if (frame_size <= current_free_space) {
                return true;
        }

        return false;
}

static void write_txbuf(struct WIZnetSocket *socket, const uint8_t *frame, const uint16_t frame_size) {
        uint8_t offsets[2];
        eth_read_mem(SN_TX_WR(socket->index), offsets, 2);
        const uint16_t current_offset = ((offsets[0] << 8) | (offsets[1])) & socket->txbuf_mask;

        if (current_offset + frame_size > socket->txbuf_size_max) {
                const size_t rest_size = socket->txbuf_size_max - current_offset;
                eth_write_mem(socket->txbuf_start + current_offset, frame, rest_size);

                eth_write_mem(socket->txbuf_start, frame + rest_size, frame_size - rest_size);
        }
        else {
                eth_write_mem(socket->txbuf_start + current_offset, frame, frame_size);
        }
}

static int read_rxbuf(struct WIZnetSocket *socket, uint8_t *buffer, const uint16_t length) {
        uint8_t offsets[2];
        eth_read_mem(SN_RX_RD(socket->index), offsets, 2);
        const uint16_t current_offset = ((offsets[0] << 8) | (offsets[1])) & socket->rxbuf_mask;

        uint8_t received[2];
        eth_read_mem(SN_RX_RSR(socket->index), received, 2);
        const uint16_t received_size = ((received[0] << 8) | (received[1]));

        uint16_t read_length;
        if (received_size > length) {
                read_length = length;
        }
        else {
                read_length = received_size;
        }

        if (current_offset + read_length > socket->rxbuf_size_max) {
                const size_t rest_size = socket->rxbuf_size_max - current_offset;

                eth_read_mem(socket->rxbuf_start + current_offset, buffer, rest_size);
                eth_read_mem(socket->rxbuf_start, buffer + rest_size, read_length - rest_size);
        }
        else {
                eth_read_mem(socket->rxbuf_start + current_offset, buffer, read_length);
        }


        return read_length;
}


static int tx_frame(struct File *socket_file, void *frame, const size_t frame_size, off_t) {
        struct WIZnetSocket *socket = (struct WIZnetSocket *) socket_file;
        if (!fits_socket_txbuf(socket, frame_size)) {
                return -EMSGSIZE;
        }

        write_txbuf(socket, frame, frame_size);

        add_to_socket_pointer_reg(socket, SN_TX_WR(socket->index), frame_size);

        socket_command(socket->index, CMD_SEND);
        const int status = wait_send_complete(socket);
        return status == 0 ? frame_size : 0;
}

static int rx_frame(struct File *socket_file, void *buf, size_t count, off_t) {
        const struct WIZnetSocket *socket = (struct WIZnetSocket *) socket_file;

        uint8_t con[1];
        eth_read_mem(SN_IR(socket->index), con, 1);
        if ((con[0] & INT_RECV) == 0) {
                struct Process *current_process = scheduler_get_current_process();
                current_process->pstate = WAITING_FOR_RESOURCE;
                wiznet_network_interface.socket_waiting_processes[socket->index] = current_process;
                save_kernelmode_and_context_switch();
        }

        //receive process
        const int res = read_rxbuf(socket, buf, count);

        add_to_socket_pointer_reg(socket, SN_RX_RD(socket->index), res);

        socket_command(socket->index, CMD_RECV);

        return res;
}


static struct Socket *create_socket(void) {
        for (size_t i = 1; i < WIZNET_SOCKET_COUNT; ++i) {
                if (wiznet_network_interface.sockets[i]->taken == false) {
                        wiznet_network_interface.sockets[i]->taken = true;
                        return (struct Socket *) wiznet_network_interface.sockets[i];
                }
        }

        return ERR_PTR(ENOSOCKFREE);
}

static struct Socket *create_raw_socket(void) {
        if (wiznet_network_interface.sockets[0]->taken) {
                return ERR_PTR(ENOSOCKFREE);
        }

        return (struct Socket *) wiznet_network_interface.sockets[0];
}

static int destroy_socket(struct Socket *sock) {
        const struct WIZnetSocket *socket = (struct WIZnetSocket *) sock;

        wiznet_network_interface.sockets[socket->index]->taken = false;

        return 0;
}

static bool is_chip_compatible(void) {
        uint8_t reg[1];
        eth_read_mem(VERR_REG, reg, 1);

        if (reg[0] == 0x51) {
                return true;
        }

        return false;
}

/**
 * Sets up interrupt mode, retransmission timeout and retries count
 */
static void setup_communication_details(void) {
        constexpr uint8_t cmd_set_mr[8] = {0x00};
        eth_write_mem(MR, cmd_set_mr, sizeof(cmd_set_mr));

        //IMR = 0 - enable sockets interrupts
        constexpr uint8_t cmd_set_imr[8] = {0x0f};
        eth_write_mem(IMR, cmd_set_imr, sizeof(cmd_set_imr));

        //RTR = 0 - 200ms retransmission timeout
        constexpr uint8_t cmd_set_rtr[8] = {0x07, 0xd0};
        eth_write_mem(RTR, cmd_set_rtr, sizeof(cmd_set_rtr));

        //RCR = 0 - 8 retries before giving up
        constexpr uint8_t cmd_set_rcr[8] = {0x08};
        eth_write_mem(RCR, cmd_set_rcr, sizeof(cmd_set_rcr));
}

/**
 * Copies network interface information from kernel struct to W5100S register
 * @param interface kernel struct with interface info
 * @return 0 on success
 */
static int setup_interface_information(struct NetworkInterface *interface) {
        eth_write_mem(SHAR, interface->mac, sizeof(interface->mac));

        eth_write_mem(GAR, interface->gateway, sizeof(interface->gateway));

        eth_write_mem(SUBR, interface->subnet_mask, sizeof(interface->subnet_mask));

        eth_write_mem(SIPR, interface->ip, sizeof(interface->ip));

        // check if the values are set up properly?
        return 0;
}

/**
 * Sets up hardware sockets on W5100S registers.
 * @return 0 on success
 */
static int init_sockets(void) {
        const struct SocketOperations wiznet_socket_operations = {
                .open = open_socket,
                .bind = bind_socket,
                .listen = listen_socket,
                .accept = accept_socket,
                .connect = connect_socket
        };

        for (int i = 0; i < 4; i++) {
                constexpr unsigned int memory_per_socket = 0x02; //2KB for each socket for TX and RX
                constexpr uint8_t cmd[] = {memory_per_socket};
                eth_write_mem(SN_TXBUF_SIZE(i), cmd, sizeof(cmd));
                eth_write_mem(SN_RXBUF_SIZE(i), cmd, sizeof(cmd));

                struct WIZnetSocket *socket = (struct WIZnetSocket *) kmalloc(sizeof(*socket));
                socket->socket.mode = CLOSED;
                socket->txbuf_mask = memory_per_socket * 1024 - 1;
                socket->txbuf_size_max = memory_per_socket * 1024;
                socket->rxbuf_mask = memory_per_socket * 1024 - 1;
                socket->rxbuf_size_max = memory_per_socket * 1024;
                socket->txbuf_start = S0_TX_BASE + (i * memory_per_socket * 1024);
                socket->rxbuf_start = S0_RX_BASE + (i * memory_per_socket * 1024);
                socket->index = i;
                socket->taken = false;

                struct SocketOperations *s_op = (struct SocketOperations *) kmalloc(sizeof(*s_op));
                *s_op = wiznet_socket_operations;
                socket->socket.s_op = s_op;

                memset(&socket->socket.file, 0, sizeof(struct File));
                struct FileOperations *f_op = (struct FileOperations *) kmalloc(sizeof(*f_op));
                memset(f_op, 0, sizeof(*f_op));
                f_op->read = rx_frame;
                f_op->write = tx_frame;
                f_op->close = active_close;

                socket->socket.file.f_op = f_op;

                wiznet_network_interface.sockets[i] = socket;
        }

        return 0;
}

struct NetworkInterface *init_ethernet(void) {
        setup_eth_gpio();

        //reset
        constexpr uint8_t cmd_rst[] = {0x80};
        eth_write_mem(MR, cmd_rst, sizeof(cmd_rst));
        delay_ms(64); //stable time from the datasheet

        if (!is_chip_compatible()) {
                return ERR_PTR(-ENODEV);
        }

        setup_communication_details();

        init_sockets();

        wiznet_network_interface.interface.setup_network_information = setup_interface_information;
        wiznet_network_interface.interface.create_socket = create_socket;
        wiznet_network_interface.interface.create_raw_socket = create_raw_socket;
        wiznet_network_interface.interface.destroy_socket = destroy_socket;
        memset(wiznet_network_interface.socket_waiting_processes,
               0,
               sizeof(wiznet_network_interface.socket_waiting_processes)
        );
        memset(wiznet_network_interface.socket_interrupts, 0, sizeof(wiznet_network_interface.socket_interrupts));

        return (struct NetworkInterface *) &wiznet_network_interface;
}
