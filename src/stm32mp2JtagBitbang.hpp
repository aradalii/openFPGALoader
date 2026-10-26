// SPDX-License-Identifier: Apache-2.0

/*
 * Copyright (C) 2026
 *
 * STM32MP2 GPIO JTAG bitbang driver.
 */

#ifndef SRC_STM32MP2JTAGBITBANG_HPP_
#define SRC_STM32MP2JTAGBITBANG_HPP_

#include <cstdint>
#include <string>

#include "board.hpp"
#include "jtagInterface.hpp"

/*
 * STM32MP2 GPIO port base addresses.
 *
 * These are deliberately compile-time constants: the JTAG wiring is fixed
 * for the target board and is not supplied by the application at runtime.
 */
#define GPIO_PORT_BASE_TCK 0x54290000 /* PORTF */
#define GPIO_PORT_BASE_TMS 0x54260000 /* PORTC */
#define GPIO_PORT_BASE_TDO 0x542B0000 /* PORTH */
#define GPIO_PORT_BASE_TDI 0x54290000 /* PORTF */

/*
 * STM32MP2 GPIO pins used by JTAG.
 */
#define TCK_PIN 8
#define TMS_PIN 11
#define TDO_PIN 6
#define TDI_PIN 7

/*
 * STM32 GPIO register offsets.
 *
 * GPIOx register layout:
 *
 *   0x00 MODER
 *   0x04 OTYPER
 *   0x08 OSPEEDR
 *   0x0c PUPDR
 *   0x10 IDR
 *   0x14 ODR
 *   0x18 BSRR
 *   0x1c LCKR
 *   0x20 AFRL
 *   0x24 AFRH
 *   0x28 BRR
 */
#define STM32_GPIO_MODER   0x00
#define STM32_GPIO_OTYPER  0x04
#define STM32_GPIO_OSPEEDR 0x08
#define STM32_GPIO_PUPDR   0x0c
#define STM32_GPIO_IDR     0x10
#define STM32_GPIO_ODR     0x14
#define STM32_GPIO_BSRR    0x18

/*!
 * \file stm32mp2JtagBitbang.hpp
 *
 * \class Stm32mp2JtagBitbang
 *
 * \brief STM32MP2 direct GPIO JTAG bitbang driver.
 *
 * JTAG pins are fixed at compile time by the GPIO_PORT_BASE_* and *_PIN
 * definitions above.
 */
class Stm32mp2JtagBitbang : public JtagInterface {
public:
	Stm32mp2JtagBitbang(const jtag_pins_conf_t *pin_conf,
			    const std::string &dev,
			    uint32_t clkHZ,
			    int8_t verbose);

	virtual ~Stm32mp2JtagBitbang();

	int setClkFreq(uint32_t clkHZ) override;

	int writeTMS(const uint8_t *tms_buf,
		     uint32_t len,
		     bool flush_buffer,
		     const uint8_t tdi = 1) override;

	int writeTDI(const uint8_t *tx,
		     uint8_t *rx,
		     uint32_t len,
		     bool end) override;

	int toggleClk(uint8_t tms,
		      uint8_t tdo,
		      uint32_t clk_len) override;

	int get_buffer_size() override { return 0; }

	bool isFull() override { return false; }

	int flush() override { return 0; }

private:
	struct gpio_regs {
		volatile uint32_t MODER;
		volatile uint32_t OTYPER;
		volatile uint32_t OSPEEDR;
		volatile uint32_t PUPDR;
		volatile uint32_t IDR;
		volatile uint32_t ODR;
		volatile uint32_t BSRR;
		volatile uint32_t LCKR;
		volatile uint32_t AFRL;
		volatile uint32_t AFRH;
		volatile uint32_t BRR;
	};

	gpio_regs volatile *_tck_port;
	gpio_regs volatile *_tms_port;
	gpio_regs volatile *_tdi_port;
	gpio_regs volatile *_tdo_port;

	/*
	 * mmap() base addresses.  TCK and TDI share GPIOF and therefore share
	 * one mapping.
	 */
	void *_tck_map;
	void *_tms_map;
	void *_tdo_map;

	int _mem_fd;
	size_t _map_size;

	int _curr_tck;
	int _curr_tms;
	int _curr_tdi;

	uint32_t _clkHZ;

	/*
	 * Half of one requested JTAG clock period, in nanoseconds.
	 *
	 * The driver uses monotonic timing and nanosleep() between GPIO
	 * transitions. nanosleep() has scheduler granularity limitations at
	 * MHz rates, so actual TCK must be measured on the hardware.
	 */
	uint64_t _half_period_ns;

	/* CLOCK_MONOTONIC timestamp of the most recent TCK edge. */
	uint64_t _last_tck_edge_ns;

	int8_t _verbose;

	void *map_gpio_port(uintptr_t physical_address);

	void configure_output(gpio_regs volatile *port, unsigned int pin);
	void configure_input(gpio_regs volatile *port, unsigned int pin);

	void set_gpio(gpio_regs volatile *port,
		      unsigned int pin,
		      bool value);

	int get_gpio(gpio_regs volatile *port,
		     unsigned int pin);

	void wait_half_period();

	int update_pins(int tck, int tms, int tdi);

	int read_tdo();

	int clock_cycle(int tms, int tdi, bool sample_tdo, uint8_t *rx, uint32_t bit);
};

#endif /* SRC_STM32MP2JTAGBITBANG_HPP_ */
