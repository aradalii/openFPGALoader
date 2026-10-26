// SPDX-License-Identifier: Apache-2.0

/*
 * Copyright (C) 2026
 *
 * STM32MP2 GPIO JTAG bitbang driver.
 */

#include "stm32mp2JtagBitbang.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <cstdio>

#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define DEBUG 1

#ifdef DEBUG
#define display(...)                                                        \
	do {                                                                    \
		if (_verbose)                                                      \
			fprintf(stdout, __VA_ARGS__);                                  \
	} while (0)
#else
#define display(...) do {} while (0)
#endif

namespace {

/*
 * Convert a GPIO pin number into the corresponding MODER field.
 *
 * STM32 GPIO MODER uses two bits per GPIO:
 *
 *   00 = input
 *   01 = general purpose output
 *   10 = alternate function
 *   11 = analog
 */
static inline uint32_t moder_mask(unsigned int pin)
{
	return 0x3U << (pin * 2U);
}

static inline uint32_t moder_output(unsigned int pin)
{
	return 0x1U << (pin * 2U);
}

} // namespace

Stm32mp2JtagBitbang::Stm32mp2JtagBitbang(
	const jtag_pins_conf_t * /*pin_conf*/,
	const std::string & /*dev*/,
	uint32_t clkHZ,
	int8_t verbose):
	_tck_port(nullptr),
	_tms_port(nullptr),
	_tdi_port(nullptr),
	_tdo_port(nullptr),
	_tck_map(nullptr),
	_tms_map(nullptr),
	_tdo_map(nullptr),
	_mem_fd(-1),
	_map_size(0),
	_curr_tck(0),
	_curr_tms(1),
	_curr_tdi(0),
	_clkHZ(0),
	_half_period_ns(0),
	_last_tck_edge_ns(0),
	_verbose(verbose > 1)
{
	const long pagesize = sysconf(_SC_PAGESIZE);

	if (pagesize <= 0)
		throw std::runtime_error("Unable to determine system page size");

	_map_size = static_cast<size_t>(pagesize);

	/*
	 * The GPIO pins are fixed by the target hardware design.
	 *
	 * TCK and TDI are both on GPIOF, so they intentionally use the same
	 * mapping.
	 */
	display(
		"STM32MP2 JTAG GPIO bitbang driver\n"
		"  TCK: GPIOF[%u] @ 0x%08lx\n"
		"  TMS: GPIOC[%u] @ 0x%08lx\n"
		"  TDO: GPIOH[%u] @ 0x%08lx\n"
		"  TDI: GPIOF[%u] @ 0x%08lx\n",
		TCK_PIN,
		static_cast<unsigned long>(GPIO_PORT_BASE_TCK),
		TMS_PIN,
		static_cast<unsigned long>(GPIO_PORT_BASE_TMS),
		TDO_PIN,
		static_cast<unsigned long>(GPIO_PORT_BASE_TDO),
		TDI_PIN,
		static_cast<unsigned long>(GPIO_PORT_BASE_TDI));

	if (clkHZ == 0)
		throw std::runtime_error("STM32MP2 JTAG frequency must be non-zero");

	/*
	 * Open physical memory.
	 *
	 * Access to /dev/mem normally requires root or an appropriately
	 * configured system.
	 */
	_mem_fd = open("/dev/mem", O_RDWR | O_SYNC);

	if (_mem_fd < 0) {
		display("Can't open /dev/mem: %s\n", strerror(errno));
		throw std::runtime_error("No access to /dev/mem");
	}

	try {
		/*
		 * GPIOF is shared by TCK and TDI.
		 *
		 * Therefore only three mmap() calls are needed:
		 *
		 *   GPIOF
		 *   GPIOC
		 *   GPIOH
		 */
		_tck_map = map_gpio_port(GPIO_PORT_BASE_TCK);
		_tms_map = map_gpio_port(GPIO_PORT_BASE_TMS);
		_tdo_map = map_gpio_port(GPIO_PORT_BASE_TDO);

		_tck_port =
			static_cast<gpio_regs volatile *>(_tck_map);

		_tdi_port =
			static_cast<gpio_regs volatile *>(_tck_map);

		_tms_port =
			static_cast<gpio_regs volatile *>(_tms_map);

		_tdo_port =
			static_cast<gpio_regs volatile *>(_tdo_map);

		/*
		 * Configure JTAG outputs.
		 *
		 * TCK starts low.
		 * TMS starts high, matching the initial JTAG reset sequence used
		 * by the generic JTAG implementation.
		 * TDI starts low.
		 */
		configure_output(_tck_port, TCK_PIN);
		configure_output(_tms_port, TMS_PIN);
		configure_output(_tdi_port, TDI_PIN);

		configure_input(_tdo_port, TDO_PIN);

		set_gpio(_tck_port, TCK_PIN, false);
		set_gpio(_tms_port, TMS_PIN, true);
		set_gpio(_tdi_port, TDI_PIN, false);

		_curr_tck = 0;
		_curr_tms = 1;
		_curr_tdi = 0;

		/*
		 * Unlike the Jetson driver, clkHZ is deliberately honoured.
		 *
		 * Jtag::Jtag() creates this object and immediately calls
		 * detectChain(), so this setting is already active while
		 * --detect is running.
		 */
		if (setClkFreq(clkHZ) != 0)
			throw std::runtime_error("Unable to configure JTAG frequency");

		/*
		 * This is the requested timing target, not a measurement of the
		 * physical TCK frequency. Userspace GPIO writes and clock_gettime()
		 * overhead can make the actual frequency lower, especially at high
		 * requested rates. Verify TCK with an oscilloscope/logic analyzer.
		 */
		display("STM32MP2 JTAG frequency target: %u Hz "
		        "(actual TCK may be lower)\n", _clkHZ);
	} catch (...) {
		if (_tck_map != nullptr)
			munmap(_tck_map, _map_size);

		if (_tms_map != nullptr)
			munmap(_tms_map, _map_size);

		if (_tdo_map != nullptr)
			munmap(_tdo_map, _map_size);

		if (_mem_fd >= 0)
			close(_mem_fd);

		_tck_map = nullptr;
		_tms_map = nullptr;
		_tdo_map = nullptr;
		_mem_fd = -1;

		throw;
	}
}

Stm32mp2JtagBitbang::~Stm32mp2JtagBitbang()
{
	/*
	 * Leave all pins as inputs before releasing the mappings.
	 *
	 * This avoids leaving the JTAG signals actively driven after the
	 * application exits.
	 */
	if (_tck_port != nullptr)
		configure_input(_tck_port, TCK_PIN);

	if (_tms_port != nullptr)
		configure_input(_tms_port, TMS_PIN);

	if (_tdi_port != nullptr)
		configure_input(_tdi_port, TDI_PIN);

	if (_tdo_port != nullptr)
		configure_input(_tdo_port, TDO_PIN);

	if (_tck_map != nullptr)
		munmap(_tck_map, _map_size);

	if (_tms_map != nullptr)
		munmap(_tms_map, _map_size);

	if (_tdo_map != nullptr)
		munmap(_tdo_map, _map_size);

	if (_mem_fd >= 0)
		close(_mem_fd);

	_tck_map = nullptr;
	_tms_map = nullptr;
	_tdo_map = nullptr;

	_tck_port = nullptr;
	_tms_port = nullptr;
	_tdi_port = nullptr;
	_tdo_port = nullptr;

	_mem_fd = -1;
}

void *Stm32mp2JtagBitbang::map_gpio_port(uintptr_t physical_address)
{
	const uintptr_t page_mask =
		static_cast<uintptr_t>(_map_size - 1);

	const uintptr_t page_address =
		physical_address & ~page_mask;

	const uintptr_t page_offset =
		physical_address & page_mask;

	void *base = mmap(
		nullptr,
		_map_size,
		PROT_READ | PROT_WRITE,
		MAP_SHARED,
		_mem_fd,
		static_cast<off_t>(page_address));

	if (base == MAP_FAILED) {
		display(
			"mmap(0x%08lx) failed: %s\n",
			static_cast<unsigned long>(physical_address),
			strerror(errno));

		throw std::runtime_error("mmap error");
	}

	return static_cast<char *>(base) + page_offset;
}

void Stm32mp2JtagBitbang::configure_output(
	gpio_regs volatile *port,
	unsigned int pin)
{
	const uint32_t mask = moder_mask(pin);

	/*
	 * General-purpose output mode.
	 */
	port->MODER =
		(port->MODER & ~mask) | moder_output(pin);

	/*
	 * Push-pull.
	 */
	port->OTYPER &= ~(1U << pin);

	/*
	 * Very-high-speed output.
	 *
	 * This does not determine the JTAG clock frequency; the bitbang
	 * timing below does that. It gives the GPIO pad sufficient slew
	 * capability for the requested clock.
	 */
	port->OSPEEDR =
		(port->OSPEEDR & ~mask) |
		(0x3U << (pin * 2U));

	/*
	 * No pull-up / pull-down.
	 */
	port->PUPDR &= ~mask;
}

void Stm32mp2JtagBitbang::configure_input(
	gpio_regs volatile *port,
	unsigned int pin)
{
	const uint32_t mask = moder_mask(pin);

	/*
	 * Input mode.
	 */
	port->MODER &= ~mask;

	/*
	 * No pull-up / pull-down.
	 */
	port->PUPDR &= ~mask;
}

void Stm32mp2JtagBitbang::set_gpio(
	gpio_regs volatile *port,
	unsigned int pin,
	bool value)
{
	/*
	 * BSRR provides an atomic GPIO set/reset operation:
	 *
	 *   bits 0..15  = set
	 *   bits 16..31 = reset
	 */
	if (value)
		port->BSRR = 1U << pin;
	else
		port->BSRR = 1U << (pin + 16U);
}

int Stm32mp2JtagBitbang::get_gpio(
	gpio_regs volatile *port,
	unsigned int pin)
{
	return (port->IDR >> pin) & 0x1U;
}

void Stm32mp2JtagBitbang::wait_half_period()
{
	/* TODO:
	* add more or little nop here for up and down jtag tclk frequency
	* based on your processor speed and PCB trace route and keep it under 5MHz for safety
	*/
	volatile int nop_delay;
	for (nop_delay = 0; nop_delay < 12; nop_delay++) {
		asm volatile("nop");
	}
}

int Stm32mp2JtagBitbang::setClkFreq(uint32_t clkHZ)
{
	if (clkHZ == 0)
		return -1;

	_clkHZ = clkHZ;

	/*
	 * One complete JTAG clock period consists of:
	 *
	 *   low -> high = half period
	 *   high -> low = half period
	 *
	 * Therefore:
	 *
	 *   half_period_ns = 1e9 / (2 * frequency)
	 *
	 * Round up rather than down so the driver never intentionally exceeds
	 * the requested JTAG frequency.
	 */
	const uint64_t denominator =
		static_cast<uint64_t>(_clkHZ) * 2ULL;

	_half_period_ns =
		(1000000000ULL + denominator - 1ULL) / denominator;

	return 0;
}

int Stm32mp2JtagBitbang::update_pins(
	int tck,
	int tms,
	int tdi)
{
	if (tdi != _curr_tdi)
		set_gpio(_tdi_port, TDI_PIN, tdi != 0);

	if (tms != _curr_tms)
		set_gpio(_tms_port, TMS_PIN, tms != 0);

	if (tck != _curr_tck) {

		// struct timespec edge_time;
		// const bool have_timestamp = clock_gettime(CLOCK_MONOTONIC, &edge_time) == 0;

		set_gpio(_tck_port, TCK_PIN, tck != 0);

		// if (have_timestamp) {
		// 	_last_tck_edge_ns =
		// 		static_cast<uint64_t>(edge_time.tv_sec) * 1000000000ULL +
		// 		static_cast<uint64_t>(edge_time.tv_nsec);
		// }
	}

	_curr_tdi = tdi;
	_curr_tms = tms;
	_curr_tck = tck;

	return 0;
}

int Stm32mp2JtagBitbang::read_tdo()
{
	return get_gpio(_tdo_port, TDO_PIN);
}

int Stm32mp2JtagBitbang::clock_cycle(
	int tms,
	int tdi,
	bool sample_tdo,
	uint8_t *rx,
	uint32_t bit)
{
	/*
	 * Set TMS/TDI while TCK is low.
	 */
	update_pins(0, tms, tdi);

	/*
	 * Low half-period.
	 */
	wait_half_period();

	/*
	 * Rising edge: JTAG samples TDI on this edge.
	 */
	update_pins(1, tms, tdi);

	/*
	 * Read TDO after the rising edge, matching the existing
	 * bitbang-driver convention.
	 */
	if (sample_tdo && rx != nullptr) {
		if (read_tdo() != 0)
			rx[bit >> 3] |=
				static_cast<uint8_t>(1U << (bit & 7U));
	}

	/*
	 * High half-period.
	 */
	wait_half_period();

	/*
	 * Falling edge / prepare next cycle.
	 */
	update_pins(0, tms, tdi);

	return 0;
}

int Stm32mp2JtagBitbang::writeTMS(
	const uint8_t *tms_buf,
	uint32_t len,
	bool /*flush_buffer*/,
	const uint8_t tdi)
{
	if (len == 0)
		return 0;

	for (uint32_t i = 0; i < len; i++) {
		const int tms =
			(tms_buf[i >> 3] & (1U << (i & 7U))) ? 1 : 0;

		clock_cycle(tms, tdi != 0, false, nullptr, 0);
        // clock_cycle(tms, 0, false, nullptr, 0);
	}

	return static_cast<int>(len);
}

int Stm32mp2JtagBitbang::writeTDI(
	const uint8_t *tx,
	uint8_t *rx,
	uint32_t len,
	bool end)
{
	const int tms_initial = _curr_tms;
	int tms = tms_initial;
	int tdi = _curr_tdi;

	if (rx != nullptr)
		std::memset(rx, 0, (len + 7U) / 8U);

	for (uint32_t i = 0; i < len; i++) {
		if (end && i == len - 1U)
			tms = 1;

		if (tx != nullptr) {
			tdi =
				(tx[i >> 3] & (1U << (i & 7U))) ? 1 : 0;
		}

		clock_cycle(tms, tdi, rx != nullptr, rx, i);
	}

	return static_cast<int>(len);
}

int Stm32mp2JtagBitbang::toggleClk(
	uint8_t tms,
	uint8_t tdi,
	uint32_t clk_len)
{
	for (uint32_t i = 0; i < clk_len; i++)
		clock_cycle(tms != 0, tdi != 0, false, nullptr, 0);

	return static_cast<int>(clk_len);
}
