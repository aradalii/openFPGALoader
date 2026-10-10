// SPDX-License-Identifier: Apache-2.0

#ifndef STM32MP2GPIODJTAGBITBANG_H
#define STM32MP2GPIODJTAGBITBANG_H

#include <string>
#include <cstdint>

#include "gpiod.h"
#include "board.hpp"
#include "jtagInterface.hpp"

struct gpiod_chip;

#ifndef GPIOD_APIV2
struct gpiod_line;
#else
struct gpiod_request_config;
struct gpiod_line_config;
struct gpiod_line_settings;
struct gpiod_line_request;
#endif

class Stm32mp2GpiodJtagBitbang : public JtagInterface {
public:
    Stm32mp2GpiodJtagBitbang(
        const jtag_pins_conf_t *pin_conf,
        const std::string &dev,
        uint32_t clkHZ,
        int8_t verbose);

    virtual ~Stm32mp2GpiodJtagBitbang();

    int setClkFreq(uint32_t clkHZ) override;

    int writeTMS(
        const uint8_t *tms_buf,
        uint32_t len,
        bool flush_buffer,
        const uint8_t tdi = 1) override;

    int writeTDI(
        const uint8_t *tx,
        uint8_t *rx,
        uint32_t len,
        bool end) override;

    int toggleClk(
        uint8_t tms,
        uint8_t tdi,
        uint32_t clk_len) override;

    bool writeTMSTDI(
        const uint8_t *tms,
        const uint8_t *tdi,
        uint8_t *tdo,
        uint32_t len) override;

    int get_buffer_size() override { return 0; }

    bool isFull() override { return false; }

    int flush() override { return 0; }

private:

#ifdef GPIOD_APIV2

    int update_pins(
        gpiod_line_value tms,
        gpiod_line_value tdi);

    int request_line(
        gpiod_chip *chip,
        unsigned int offset,
        gpiod_line_direction direction,
        gpiod_line_value initial,
        gpiod_line_request **request,
        gpiod_line_config **line_cfg,
        gpiod_line_settings **settings);

#else

    gpiod_line *get_line(
        gpiod_chip *chip,
        unsigned int offset,
        int val,
        int dir);

    int update_pins(
        int tck,
        int tms,
        int tdi);

#endif

    int read_tdo();

    bool _verbose;

    /*
     * Fixed STM32MP2 GPIO mapping.
     *
     * TDI = PF7 = gpiochip5 line 7
     * TCK = PF8 = gpiochip5 line 8
     * TMS = PC11 = gpiochip2 line 11
     * TDO = PH6 = gpiochip7 line 6
     */

    static constexpr const char *TDI_CHIP = "/dev/gpiochip5";
    static constexpr const char *TCK_CHIP = "/dev/gpiochip5";
    static constexpr const char *TMS_CHIP = "/dev/gpiochip2";
    static constexpr const char *TDO_CHIP = "/dev/gpiochip7";

    static constexpr unsigned int TDI_PIN = 7;
    static constexpr unsigned int TCK_PIN = 8;
    static constexpr unsigned int TMS_PIN = 11;
    static constexpr unsigned int TDO_PIN = 6;

    gpiod_chip *_tck_chip;
    gpiod_chip *_tms_chip;
    gpiod_chip *_tdi_chip;
    gpiod_chip *_tdo_chip;

#ifdef GPIOD_APIV2

    gpiod_request_config *_tck_req_cfg;
    gpiod_request_config *_tms_req_cfg;
    gpiod_request_config *_tdi_req_cfg;
    gpiod_request_config *_tdo_req_cfg;

    gpiod_line_config *_tck_line_cfg;
    gpiod_line_config *_tms_line_cfg;
    gpiod_line_config *_tdi_line_cfg;
    gpiod_line_config *_tdo_line_cfg;

    gpiod_line_settings *_tck_settings;
    gpiod_line_settings *_tms_settings;
    gpiod_line_settings *_tdi_settings;
    gpiod_line_settings *_tdo_settings;

    gpiod_line_request *_tck_request;
    gpiod_line_request *_tms_request;
    gpiod_line_request *_tdi_request;
    gpiod_line_request *_tdo_request;

    gpiod_line_value _curr_tms;
    gpiod_line_value _curr_tdi;

#else

    gpiod_line *_tck_line;
    gpiod_line *_tms_line;
    gpiod_line *_tdo_line;
    gpiod_line *_tdi_line;

    int _curr_tms;
    int _curr_tdi;

#endif

    int _curr_tck;
};

#endif
