//
// Created by Artur Twardzik on 30/12/2024.
//

#ifndef TIME_H
#define TIME_H

void setup_internal_clk(void);

void delay_ms(unsigned int ms);

void delay_us(unsigned int us);

void ms_since_boot(uint64_t *time);

#endif //TIME_H
