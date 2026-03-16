#ifndef _GPIO_H_
#define _GPIO_H_

#include <stdint.h>

// Memory map-Mapped I/O base addresses for the BCM2711 SoC used in Raspberry Pi 4.

// Pi 2, 3, 4 have different peripheral base addresses, but the GPIO registers are always at offset 0x200000 from the peripheral base address.
#define BCM2708_PERI_BASE        0x20000000  // Pi 2 peripheral base address
#define BCM2709_PERI_BASE        0x3F000000  // Pi 3 peripheral base address
#define BCM2711_PERI_BASE        0xFE000000  // Pi 4 peripheral base address

#define BCM_BASE BCM2711_PERI_BASE

// GPIO register offsets
#define GPIO_BASE (BCM_BASE + 0x200000)
// The BCM2711 has a microsecond timer at offset 0x3000 from the peripheral base
#define TIMER_CTRL (BCM_BASE + 0x3000)

// GPFSEL: GPIO Function Select each pin has 3 bits to select its function, so each GPFSEL register controls 10 pins
// (10*3=30 bits)
#define GPFSEL0      0
#define GPFSEL1      1
#define GPFSEL2      2
#define GPFSEL3      3
#define GPFSEL4      4
#define GPFSEL5      5
//GPSET: GPIO Pin Output Write 1 gpio to high, set 0 has no effect
#define GPSET0       7
#define GPSET1       8
//GPCLR: GPIO Pin Output Clear set 1 gpio to low, set 0 has no effect
#define GPCLR0       10
#define GPCLR1       11
// GPLEV: GPIO Pin Level  1 for high and 0 for low
#define GPLEV0       13
#define GPLEV1       14

// if using Pi4, the pull-up/down registers are GPPUPPDN0-3, each controlling 16 pins with 2 bits per pin (16*2=32 bits)
#if (BCM_BASE) == (BCM2711_PERI_BASE)
#define GPPUPPDN0 57
#define GPPUPPDN1 58
#define GPPUPPDN2 59
#define GPPUPPDN3 60
#else
// Older BCMs have a different pull-up/down register layout, with only two registers for all 54 GPIO pins.
#define GPPUD        37
#define GPPUDCLK0    38
#define GPPUDCLK1    39
#endif


extern volatile uint32_t *gpio_base;
extern volatile uint32_t *timer_uS;
// Busy wait for the specified number of microseconds using the BCM2711's microsecond timer
static inline void gpio_busy_wait(uint32_t uS) {
    uint32_t start = *timer_uS;
    while (*timer_uS - start <= uS);
}
// Set or clear multiple GPIO pins at once using bitmasks
static inline void gpio_set_bits(uint32_t bits) {
    gpio_base[GPSET0] = bits;
}
static inline void gpio_clear_bits(uint32_t bits) {
    gpio_base[GPCLR0] = bits;
}
// Set, clear, or read individual GPIO pins
static inline void gpio_set_pin(int pin) {
    gpio_set_bits(1ul << pin);
}
static inline void gpio_clear_pin(int pin) {
    gpio_clear_bits(1ul << pin);
}
// Read the level of multiple GPIO pins at once using a bitmask, or read an individual pin
static inline uint32_t gpio_get_bits(uint32_t bits) {
    return gpio_base[GPLEV0] & bits;
}
static inline int gpio_get_pin(int pin) {
    return gpio_get_bits(1ul << pin) != 0;
}

void gpio_init_pull(int pin, int pud);
void gpio_init_in(int pin);
void gpio_init_out(int pin);

bool gpio_init(void);

#endif

