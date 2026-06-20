#include "libc.h"
#include "socket.h"

#define RESET "\033[0m"
#define BOLD "" /*"\033[1m"*/

#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"
#define CYAN "\033[36m"
#define MAGENTA "\033[35m"

struct config_t {
        char *address;
        int port;
        int unit_id;

        int function;
        int reg;
        int count;
        int value;

        char *rawdata;

        int timeout;
        bool debug;
};

void print_help_menu(void) {
        printf(BOLD CYAN "\n=== MODBUS TCP CLI TOOL ===\n" RESET);

        printf(BOLD GREEN "\nWhat is Modbus TCP?\n" RESET);
        printf("Modbus TCP is a communication protocol running over TCP/IP (port 502).\n");
        printf("It is widely used in industrial automation systems.\n");
        printf("This program acts as a " YELLOW "client (master)" RESET " sending requests to devices.\n");

        printf(BOLD GREEN "\nHow does it work?\n" RESET);
        printf("1. Connect to a device (IP + port)\n");
        printf("2. Send a Modbus request frame\n");
        printf("3. Receive response data or status\n");

        printf(BOLD GREEN "\nModbus TCP Frame Structure:\n" RESET);

        printf(BOLD MAGENTA "\n[ Modbus Application Protocol Header (MBAP) ]\n" RESET);
        printf(CYAN "Transaction ID " RESET "- 2 bytes (request identifier)\n");
        printf(CYAN "Protocol ID    " RESET "- 2 bytes (always 0)\n");
        printf(CYAN "Length         " RESET "- 2 bytes (remaining length)\n");
        printf(CYAN "Unit ID        " RESET "- 1 byte (slave ID)\n");

        printf(BOLD MAGENTA "\n[ Protocol Data Unit (PDU) ]\n" RESET);
        printf(CYAN "Function Code  " RESET "- 1 byte (e.g. 3 = read registers)\n");
        printf(CYAN "Data           " RESET "- depends on function (max 260B)\n");

        printf(BOLD GREEN "\nExample Modbus Functions:\n" RESET);
        printf("  " CYAN "1" RESET "  - Read Coils\n");
        printf("  " CYAN "2" RESET "  - Read Discrete Inputs\n");
        printf("  " CYAN "3" RESET "  - Read Holding Registers\n");
        printf("  " CYAN "4" RESET "  - Read Input Registers\n");
        printf("  " CYAN "5" RESET "  - Write Single Coil\n");
        printf("  " CYAN "6" RESET "  - Write Single Register\n");
        printf("  " CYAN "16" RESET " - Write Multiple Registers\n");

        printf(BOLD GREEN "\nRAW mode:\n" RESET);
        printf("You can send a custom raw frame as hex:\n");
        printf(YELLOW "--rawdata \"00 01 00 00 00 06 01 03 00 00 00 02\"\n" RESET);
        printf("The program will send these bytes exactly as provided.\n");

        printf(BOLD GREEN "\nUsage:\n" RESET);

        printf(BOLD "\nRead registers:\n" RESET);
        printf(GREEN "./modbustcp -a 192.168.0.10 -u 1 -f 3 -r 0 -c 10\n" RESET);

        printf(BOLD "\nWrite register:\n" RESET);
        printf(GREEN "./modbustcp -a 192.168.0.10 -u 1 -f 6 -r 0 -v 123\n" RESET);

        printf(BOLD "\nRAW request:\n" RESET);
        printf(GREEN "./modbustcp -a 192.168.0.10 --rawdata \"00 01 00 00 00 06 01 03 00 00 00 02\"\n" RESET);

        printf(BOLD GREEN "\nOptions:\n" RESET);
        printf("  -a, --address     Device IP address\n");
        printf("  -p, --port        Port (default 502)\n");
        printf("  -u, --unit-id     Unit ID\n");
        printf("  -f, --function    Modbus function code\n");
        printf("  -r, --register    Register address\n");
        printf("  -c, --count       Number of registers\n");
        printf("  -v, --value       Value (for write)\n");
        printf("  -w, --rawdata     Raw HEX frame\n");
        printf("  -t, --timeout     Timeout (ms)\n");
        printf("  -d, --debug       Debug mode\n");
        printf("  -h, --help        Show help\n");

        printf(BOLD GREEN "\nNOTES:\n" RESET);

        printf(BOLD CYAN "\n============================\n\n" RESET);
}

void print_debug(struct config_t *cfg) {
        printf(BOLD GREEN "==== DEBUG INFO ====\n" RESET);
        printf(BOLD YELLOW "address: %s\n" RESET, cfg->address);
        printf(BOLD YELLOW "port: %d\n\n" RESET, cfg->port);
        printf(BOLD CYAN "unit_id: %d\n" RESET, cfg->unit_id);
        printf(BOLD CYAN "function: %d\n" RESET, cfg->function);
        printf(BOLD CYAN "reg: %d\n" RESET, cfg->reg);
        printf(BOLD CYAN "count: %d\n" RESET, cfg->count);
        printf(BOLD CYAN "value: %d\n\n" RESET, cfg->value);
        printf(BOLD MAGENTA "rawdata: %s\n\n" RESET, cfg->rawdata);
        printf(BOLD MAGENTA "timeout: %d\n" RESET, cfg->timeout);
        printf(BOLD MAGENTA "debug: %s\n", cfg->debug ? BOLD GREEN "true" RESET : BOLD RED "false" RESET);
}

int build_frame(struct config_t *cfg, uint8_t *frame) {
        // kod ukradziony gdzieś z czeluści internetu
        int i = 0;

        // MBAP
        frame[i++] = 0;
        frame[i++] = 1;
        frame[i++] = 0;
        frame[i++] = 0;

        int len_pos = i;
        frame[i++] = 0;
        frame[i++] = 0;

        frame[i++] = (uint8_t) cfg->unit_id;

        // PDU
        frame[i++] = (uint8_t) cfg->function;

        if (cfg->function == 3 || cfg->function == 4) {
                frame[i++] = cfg->reg >> 8;
                frame[i++] = cfg->reg & 0xFF;

                frame[i++] = cfg->count >> 8;
                frame[i++] = cfg->count & 0xFF;
        }
        else if (cfg->function == 6) {
                frame[i++] = cfg->reg >> 8;
                frame[i++] = cfg->reg & 0xFF;

                frame[i++] = cfg->value >> 8;
                frame[i++] = cfg->value & 0xFF;
        }

        // length = reszta po MBAP (UnitID + PDU)
        uint16_t len = i - 6;

        frame[len_pos] = len >> 8;
        frame[len_pos + 1] = len & 0xFF;

        return i;
}

int parse_hex(const char *str, uint8_t *out, const size_t length) {
        int index = 0;

        const char *ptr = str;
        while (*ptr) {
                while (*ptr == ' ') {
                        ptr += 1;
                }

                if (*ptr == '\0') {
                        break;
                }

                const char byte[3] = {*ptr, *(ptr + 1), 0};
                const unsigned char value = (unsigned char) strtoul(byte, nullptr, 16);

                out[index++] = value;

                ptr += 2;
        }

        return index;
}

int send_modbus(struct config_t *cfg) {
        // --- SOCKET SETUP ---
        const int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
                dprintf(2, BOLD RED "?? socket error, could not open a TCP socket ??\n" RESET);
                return 1;
        }
        struct sockaddr_in source = {AF_INET, htons(8080)};
        if (bind(sock, (struct sockaddr *) &source, sizeof(source)) < 0) {
                dprintf(2, BOLD RED "?? socket error, could not bind to port %d ??\n" RESET, 8080);
                return 1;
        }

        struct sockaddr_in remote = {0};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(cfg->port);
        if (inet_aton(cfg->address, &remote.sin_addr) != 1) {
                dprintf(2, BOLD RED "?? invalid address: %s ??\n" RESET, cfg->address);
                close(sock);
                return 1;
        }

        int i = 0;
        while (connect(sock, (struct sockaddr *) &remote, sizeof(remote)) < 0 && i++ < 10) {
                dprintf(2, BOLD RED "?? connect error ??" RESET);
                if (i == 10) {
                        dprintf(2, BOLD RED "?? host unreachable ??\n" RESET);
                        return 1;
                }
        }


        // --- SEND DATA ---
        uint8_t *buffer;
        size_t len;
        uint8_t frame[260];

        uint8_t rawbuf[260];

        if (cfg->rawdata != nullptr) {
                len = parse_hex(cfg->rawdata, rawbuf, 260);

                if (len == 0) {
                        dprintf(2, BOLD RED "?? Invalid hex format ??\n" RESET);
                        return 1;
                }

                buffer = rawbuf;
        }
        else {
                len = build_frame(cfg, frame);
                buffer = frame;
        }

        size_t total = 0;

        while (total < len) {
                const ssize_t n = write(sock, buffer + total, len - total);
                if (n <= 0) {
                        dprintf(2, BOLD RED "?? write error ??" RESET);
                        close(sock);
                        return 1;
                }
                total += n;
        }

        printf(BOLD GREEN "sent (%i bytes)\n" RESET, (int) total);

        close(sock);

        return 0;
}


int main(int argc, char *argv[]) {
        if (argc == 1) {
                printf(BOLD GREEN "Try -h or --help\n");
                return 0;
        }

        struct config_t cfg = {0};
        cfg.address = "127.0.0.1";
        cfg.port = 502;
        cfg.unit_id = 1;
        cfg.timeout = 1000;
        cfg.debug = false;

        char *filename;
        bool filechosen = false;

        int c;
        while ((c = getopt(argc, argv, "ha:p:u:f:r:c:v:w:t:F:d")) != -1) {
                switch (c) {
                        case 'a':
                                cfg.address = optargs;
                                break;
                        case 'p':
                                cfg.port = strtoul(optargs, nullptr, 10);
                                break;
                        case 'u':
                                cfg.unit_id = strtoul(optargs, nullptr, 10);
                                break;

                        case 'f':
                                cfg.function = strtoul(optargs, nullptr, 10);
                                break;
                        case 'r':
                                cfg.reg = strtoul(optargs, nullptr, 10);
                                break;
                        case 'c':
                                cfg.count = strtoul(optargs, nullptr, 10);
                                break;
                        case 'v':
                                cfg.value = strtoul(optargs, nullptr, 10);
                                break;

                        case 'w':
                                cfg.rawdata = optargs;
                                break;

                        case 't':
                                cfg.timeout = strtoul(optargs, nullptr, 10);
                                break;
                        case 'd':
                                cfg.debug = true;

                                break;
                        case 'F':
                                filename = optargs;
                                filechosen = true;
                                break;
                        case 'h':
                                print_help_menu();
                                exit(0);
                                break;
                        case '?':
                                dprintf(2, BOLD RED "?? Unknown option ??\n" RESET);
                                exit(1);
                                break;
                        default:
                                exit(1);
                }
        }


        if (cfg.debug) {
                print_debug(&cfg);
        }

        if (filechosen) {
                if (filechosen) {
                        int fd = open(filename, O_RDONLY, 0); // O_RDONLY
                        if (fd < 0) {
                                dprintf(2, "Could not open file %s\n", filename);
                                return 1;
                        }

                        char line_buffer[64];
                        int pos = 0;
                        char c;


                        while (read(fd, &c, 1) > 0) {
                                if (c == '\n' || c == '\r' || pos >= (int) sizeof(line_buffer) - 1) {
                                        if (pos > 0) {
                                                line_buffer[pos] = '\0';

                                                char *colonPtr = strchr(line_buffer, ':');
                                                if (colonPtr != nullptr) {
                                                        *colonPtr = '\0';

                                                        cfg.address = line_buffer;
                                                        cfg.port = (int) strtoul(colonPtr + 1, nullptr, 10);

                                                        printf(BOLD "Sending to: %s:%i\n" RESET, cfg.address, cfg.port);
                                                        send_modbus(&cfg);
                                                }
                                                pos = 0;
                                        }
                                }
                                else {
                                        line_buffer[pos++] = c;
                                }
                        }

                        // if end without enter
                        if (pos > 0) {
                                line_buffer[pos] = '\0';
                                char *colonPtr = strchr(line_buffer, ':');
                                if (colonPtr != nullptr) {
                                        *colonPtr = '\0';
                                        cfg.address = line_buffer;
                                        cfg.port = (int) strtoul(colonPtr + 1, nullptr, 10);
                                        send_modbus(&cfg);
                                }
                        }

                        close(fd);
                }
        }
        else {
                send_modbus(&cfg);
        }


        return 0;
}
