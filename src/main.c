#include "libc.h"
#include "tty.h"
#include "drivers/gpio.h"
#include "drivers/sd_card.h"
#include "drivers/spi.h"
#include "drivers/time.h"
#include "fs/file.h"
#include "fs/mbr.h"
#include "fs/ramfs.h"
#include "kernel/error.h"
#include "kernel/memory.h"
#include "kernel/network.h"
#include "kernel/proc.h"
#include "kernel/resets.h"

#ifdef TESTS
#include "tests.h"
#endif

extern void PATER_ADAMVS(int argc, char *argv[]);

int main(void) {
#ifdef STANDALONE
        setup_internal_clk();
        reset_subsys();

        GPIO_function_select(15, 1); // SD_SPI_TX
        GPIO_function_select(14, 1); // SD_SPI_CLK
        init_pin_output(13);         // SD_CS
        GPIO_function_select(12, 1); // SD_SPI_RX
        output_enable_pin(15);
        output_enable_pin(14);
        output_enable_pin(12);
        spi_init(1, 2, 191, 0); // 150[MHz] / 2*(1+3) = 390.625 [kHz]
#endif
        init_tty();
        printk("   --- \x1b[91;49mG\x1b[93;49me\x1b[92;49mT \x1b[94;49mO\x1b[95;49mS\x1b[0m Kernel startup ---\n\n");
        int res;

        init_pin_output(25);
        init_pin_output(11);

        printk_status_init("Initializing scheduler");
        void *msp;
        __asm__("mrs    %0, msp" : "=r"(msp));
        res = scheduler_init(msp);
        printk_status_finish(res);


        printk_status_init("Mounting ramfs");
        struct Dentry *root = ramfs_mount(nullptr, nullptr, nullptr, 0);
        if (!root) {
                kernel_panic("RAMFS could not be mounted.", __FILE__, __LINE__, __func__);
        }
        printk_status_step();
        constexpr size_t root_dirs_count = 2;
        const char *root_dirs[root_dirs_count] = {"dev", "mnt"};
        for (size_t i = 0; i < root_dirs_count; ++i) {
                struct Dentry file = {
                        .name = root_dirs[i],
                        .inode = root->inode->i_sb->s_op->alloc_inode(root->inode->i_sb),
                };

                root->inode->i_op->create(root->inode, &file, S_IFDIR | 0666);
        }
        printk_status_step();

        struct Dentry dev = {.name = "dev"};
        root->inode->i_op->lookup(root->inode, &dev, 0);
        struct Dentry tty_dentry = {
                .name = "tty0",
                .inode = root->inode->i_sb->s_op->alloc_inode(root->inode->i_sb),
        };
        dev.inode->i_op->create(dev.inode, &tty_dentry, S_IFCHR | 0666);
        struct Dentry *tty = dev.inode->i_op->lookup(dev.inode, &tty_dentry, 0);
        printk_status_step();

        res = setup_tty_chrfile(tty->inode);
        printk_status_finish(res);

        struct HardDriveOperations *sd_op = init_sd_card();
        struct Dentry mnt = {.name = "mnt"};
        root->inode->i_op->lookup(root->inode, &mnt, 0);
        if (sd_op && mnt.inode) {
                struct PartitionTableEntry *partition_table = get_mbr_partition_table(sd_op);

                for (int i = 0; i < 4; ++i) {
                        mount_partition(&mnt, partition_table[i].lba_start, sd_op);
                }

                kfree(partition_table);
        }

        init_network();

#ifdef TESTS
        run_kernelspace_tests();
#endif

        printk_status_init("Creating process init");
        res = create_process_init((void (*)(void)) PATER_ADAMVS, root->inode);
        printk_status_finish(res);

        printk("\n   --- Switching to process init ---\n\n");
        run_process_init();
        return 0;
}
