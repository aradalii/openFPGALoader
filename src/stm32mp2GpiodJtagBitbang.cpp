// SPDX-License-Identifier: Apache-2.0

#include "stm32mp2GpiodJtagBitbang.hpp"

#include <gpiod.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "display.hpp"


#define DEBUG 1

#ifdef DEBUG
#define display(...) \
    do { \
        if (_verbose) \
            fprintf(stdout, __VA_ARGS__); \
    } while (0)
#else
#define display(...) do {} while (0)
#endif


Stm32mp2GpiodJtagBitbang::Stm32mp2GpiodJtagBitbang(
        const jtag_pins_conf_t *pin_conf,
        const std::string &dev,
        uint32_t clkHZ,
        int8_t verbose)
    : _verbose(verbose > 1),
      _tck_chip(nullptr),
      _tms_chip(nullptr),
      _tdi_chip(nullptr),
      _tdo_chip(nullptr),
      _curr_tck(0)
{
    (void)pin_conf;
    (void)dev;

#ifdef GPIOD_APIV2

    _tck_req_cfg = nullptr;
    _tms_req_cfg = nullptr;
    _tdi_req_cfg = nullptr;
    _tdo_req_cfg = nullptr;

    _tck_line_cfg = nullptr;
    _tms_line_cfg = nullptr;
    _tdi_line_cfg = nullptr;
    _tdo_line_cfg = nullptr;

    _tck_settings = nullptr;
    _tms_settings = nullptr;
    _tdi_settings = nullptr;
    _tdo_settings = nullptr;

    _tck_request = nullptr;
    _tms_request = nullptr;
    _tdi_request = nullptr;
    _tdo_request = nullptr;

    _curr_tms = GPIOD_LINE_VALUE_ACTIVE;
    _curr_tdi = GPIOD_LINE_VALUE_INACTIVE;

#else

    _tck_line = nullptr;
    _tms_line = nullptr;
    _tdo_line = nullptr;
    _tdi_line = nullptr;

    _curr_tms = 1;
    _curr_tdi = 0;

#endif

    display(
        "STM32MP2 GPIO JTAG:\n"
        "  TDI = %s[%u] PF7\n"
        "  TCK = %s[%u] PF8\n"
        "  TMS = %s[%u] PC11\n"
        "  TDO = %s[%u] PH6\n",
        TDI_CHIP, TDI_PIN,
        TCK_CHIP, TCK_PIN,
        TMS_CHIP, TMS_PIN,
        TDO_CHIP, TDO_PIN);


    /*
     * Open GPIO chips.
     *
     * PF7 and PF8 share gpiochip5.
     */
    _tck_chip = gpiod_chip_open(TCK_CHIP);

    if (!_tck_chip) {
        throw std::runtime_error(
            "Unable to open /dev/gpiochip5");
    }

    _tdi_chip = _tck_chip;


    /*
     * PC11.
     */
    _tms_chip = gpiod_chip_open(TMS_CHIP);

    if (!_tms_chip) {
        gpiod_chip_close(_tck_chip);

        _tck_chip = nullptr;
        _tdi_chip = nullptr;

        throw std::runtime_error(
            "Unable to open /dev/gpiochip2");
    }


    /*
     * PH6.
     */
    _tdo_chip = gpiod_chip_open(TDO_CHIP);

    if (!_tdo_chip) {
        gpiod_chip_close(_tms_chip);
        gpiod_chip_close(_tck_chip);

        _tms_chip = nullptr;
        _tck_chip = nullptr;
        _tdi_chip = nullptr;

        throw std::runtime_error(
            "Unable to open /dev/gpiochip7");
    }


#ifdef GPIOD_APIV2

    /*
     * libgpiod v2.
     *
     * Each GPIO chip needs its own line request.
     *
     * We deliberately use one request per signal. This is not
     * optimized yet; the goal is to get the board working first.
     */

    if (request_line(
            _tck_chip,
            TCK_PIN,
            GPIOD_LINE_DIRECTION_OUTPUT,
            GPIOD_LINE_VALUE_INACTIVE,
            &_tck_request,
            &_tck_line_cfg,
            &_tck_settings) < 0)
        throw std::runtime_error("Unable to request TCK");

    if (request_line(
            _tdi_chip,
            TDI_PIN,
            GPIOD_LINE_DIRECTION_OUTPUT,
            GPIOD_LINE_VALUE_INACTIVE,
            &_tdi_request,
            &_tdi_line_cfg,
            &_tdi_settings) < 0)
        throw std::runtime_error("Unable to request TDI");

    if (request_line(
            _tms_chip,
            TMS_PIN,
            GPIOD_LINE_DIRECTION_OUTPUT,
            GPIOD_LINE_VALUE_ACTIVE,
            &_tms_request,
            &_tms_line_cfg,
            &_tms_settings) < 0)
        throw std::runtime_error("Unable to request TMS");

    if (request_line(
            _tdo_chip,
            TDO_PIN,
            GPIOD_LINE_DIRECTION_INPUT,
            GPIOD_LINE_VALUE_INACTIVE,
            &_tdo_request,
            &_tdo_line_cfg,
            &_tdo_settings) < 0)
        throw std::runtime_error("Unable to request TDO");

#else

    /*
     * libgpiod v1.
     */

    _tck_line = get_line(
        _tck_chip,
        TCK_PIN,
        0,
        GPIOD_LINE_REQUEST_DIRECTION_OUTPUT);

    _tdi_line = get_line(
        _tdi_chip,
        TDI_PIN,
        0,
        GPIOD_LINE_REQUEST_DIRECTION_OUTPUT);

    _tms_line = get_line(
        _tms_chip,
        TMS_PIN,
        1,
        GPIOD_LINE_REQUEST_DIRECTION_OUTPUT);

    _tdo_line = get_line(
        _tdo_chip,
        TDO_PIN,
        0,
        GPIOD_LINE_REQUEST_DIRECTION_INPUT);

#endif

    _clkHZ = clkHZ ? clkHZ : 5000000;
}


#ifdef GPIOD_APIV2

int Stm32mp2GpiodJtagBitbang::request_line(
    gpiod_chip *chip,
    unsigned int offset,
    gpiod_line_direction direction,
    gpiod_line_value initial,
    gpiod_line_request **request,
    gpiod_line_config **line_cfg,
    gpiod_line_settings **settings)
{
    *settings = gpiod_line_settings_new();

    if (!*settings)
        return -1;

    if (gpiod_line_settings_set_direction(
            *settings,
            direction) < 0)
        return -1;

    /*
     * Disable bias for the JTAG pins.
     */
    if (gpiod_line_settings_set_bias(
            *settings,
            GPIOD_LINE_BIAS_DISABLED) < 0)
        return -1;

    if (direction == GPIOD_LINE_DIRECTION_OUTPUT) {
        if (gpiod_line_settings_set_output_value(
                *settings,
                initial) < 0)
            return -1;
    }


    *line_cfg = gpiod_line_config_new();

    if (!*line_cfg)
        return -1;


    if (gpiod_line_config_add_line_settings(
            *line_cfg,
            &offset,
            1,
            *settings) < 0)
        return -1;


    gpiod_request_config *req_cfg =
        gpiod_request_config_new();

    if (!req_cfg)
        return -1;

    gpiod_request_config_set_consumer(
        req_cfg,
        "openFPGALoader-stm32mp2");


    *request = gpiod_chip_request_lines(
        chip,
        req_cfg,
        *line_cfg);

    /*
     * request config is no longer needed after
     * gpiod_chip_request_lines().
     */
    gpiod_request_config_free(req_cfg);

    if (!*request)
        return -1;

    return 0;
}


int Stm32mp2GpiodJtagBitbang::update_pins(
    gpiod_line_value tms,
    gpiod_line_value tdi)
{
    if (tdi != _curr_tdi) {
        if (gpiod_line_request_set_value(
                _tdi_request,
                TDI_PIN,
                tdi) < 0) {

            display("Unable to set TDI\n");
            return -1;
        }
    }

    if (tms != _curr_tms) {
        if (gpiod_line_request_set_value(
                _tms_request,
                TMS_PIN,
                tms) < 0) {

            display("Unable to set TMS\n");
            return -1;
        }
    }

    if (_curr_tck != 0) {
        if (gpiod_line_request_set_value(
                _tck_request,
                TCK_PIN,
                GPIOD_LINE_VALUE_INACTIVE) < 0) {

            display("Unable to set TCK low\n");
            return -1;
        }
    }

    _curr_tck = 0;

    _curr_tdi = tdi;
    _curr_tms = tms;

    return 0;
}

#else

gpiod_line *Stm32mp2GpiodJtagBitbang::get_line(
    gpiod_chip *chip,
    unsigned int offset,
    int val,
    int dir)
{
    gpiod_line *line =
        gpiod_chip_get_line(chip, offset);

    if (!line) {
        display(
            "Unable to get gpio line %u\n",
            offset);

        throw std::runtime_error(
            "Unable to get gpio line");
    }


    gpiod_line_request_config config = {
        .consumer = "openFPGALoader-stm32mp2",
        .request_type = dir,
        .flags = 0,
    };


    if (gpiod_line_request(
            line,
            &config,
            val) < 0) {

        display(
            "Error requesting gpio line %u\n",
            offset);

        throw std::runtime_error(
            "Error requesting gpio line");
    }

    return line;
}


int Stm32mp2GpiodJtagBitbang::update_pins(
    int tck,
    int tms,
    int tdi)
{
    if (tdi != _curr_tdi) {
        if (gpiod_line_set_value(
                _tdi_line,
                tdi) < 0) {

            display("Unable to set TDI\n");
            return -1;
        }
    }

    if (tms != _curr_tms) {
        if (gpiod_line_set_value(
                _tms_line,
                tms) < 0) {

            display("Unable to set TMS\n");
            return -1;
        }
    }

    if (tck != _curr_tck) {
        if (gpiod_line_set_value(
                _tck_line,
                tck) < 0) {

            display("Unable to set TCK\n");
            return -1;
        }
    }

    _curr_tck = tck;
    _curr_tdi = tdi;
    _curr_tms = tms;

    return 0;
}

#endif


Stm32mp2GpiodJtagBitbang::~Stm32mp2GpiodJtagBitbang()
{
#ifdef GPIOD_APIV2

    if (_tck_request)
        gpiod_line_request_release(_tck_request);

    if (_tdi_request)
        gpiod_line_request_release(_tdi_request);

    if (_tms_request)
        gpiod_line_request_release(_tms_request);

    if (_tdo_request)
        gpiod_line_request_release(_tdo_request);

    if (_tck_line_cfg)
        gpiod_line_config_free(_tck_line_cfg);

    if (_tdi_line_cfg)
        gpiod_line_config_free(_tdi_line_cfg);

    if (_tms_line_cfg)
        gpiod_line_config_free(_tms_line_cfg);

    if (_tdo_line_cfg)
        gpiod_line_config_free(_tdo_line_cfg);

    if (_tck_settings)
        gpiod_line_settings_free(_tck_settings);

    if (_tdi_settings)
        gpiod_line_settings_free(_tdi_settings);

    if (_tms_settings)
        gpiod_line_settings_free(_tms_settings);

    if (_tdo_settings)
        gpiod_line_settings_free(_tdo_settings);

#else

    if (_tck_line)
        gpiod_line_release(_tck_line);

    if (_tdi_line)
        gpiod_line_release(_tdi_line);

    if (_tms_line)
        gpiod_line_release(_tms_line);

    if (_tdo_line)
        gpiod_line_release(_tdo_line);

#endif

    /*
     * tck_chip and tdi_chip are the same object.
     */
    if (_tms_chip)
        gpiod_chip_close(_tms_chip);

    if (_tdo_chip)
        gpiod_chip_close(_tdo_chip);

    if (_tck_chip)
        gpiod_chip_close(_tck_chip);
}


int Stm32mp2GpiodJtagBitbang::read_tdo()
{
#ifdef GPIOD_APIV2

    gpiod_line_value value =
        gpiod_line_request_get_value(
            _tdo_request,
            TDO_PIN);

    if (value == GPIOD_LINE_VALUE_ERROR) {
        display("Error reading TDO\n");
        return -1;
    }

    return value == GPIOD_LINE_VALUE_ACTIVE ? 1 : 0;

#else

    return gpiod_line_get_value(_tdo_line);

#endif
}


int Stm32mp2GpiodJtagBitbang::setClkFreq(
    __attribute__((unused)) uint32_t clkHZ)
{
    /*
     * libgpiod userspace bit-banging determines the
     * practical clock frequency.
     */
    return 0;
}


int Stm32mp2GpiodJtagBitbang::writeTMS(
    const uint8_t *tms_buf,
    uint32_t len,
    __attribute__((unused)) bool flush_buffer,
    __attribute__((unused)) uint8_t tdi)
{
    if (len == 0)
        return 0;

    for (uint32_t i = 0; i < len; i++) {

#ifdef GPIOD_APIV2

        gpiod_line_value tms =
            (tms_buf[i >> 3] & (1 << (i & 7))) ?
            GPIOD_LINE_VALUE_ACTIVE :
            GPIOD_LINE_VALUE_INACTIVE;

        update_pins(tms, _curr_tdi);

        gpiod_line_request_set_value(
            _tck_request,
            TCK_PIN,
            GPIOD_LINE_VALUE_ACTIVE);

        _curr_tck = 1;

#else

        int tms =
            (tms_buf[i >> 3] & (1 << (i & 7))) ?
            1 : 0;

        update_pins(0, tms, _curr_tdi);
        update_pins(1, tms, _curr_tdi);

#endif
    }

    return len;
}


int Stm32mp2GpiodJtagBitbang::writeTDI(
    const uint8_t *tx,
    uint8_t *rx,
    uint32_t len,
    bool end)
{
#ifdef GPIOD_APIV2
    gpiod_line_value tdi =
        GPIOD_LINE_VALUE_INACTIVE;
#else
    int tms = _curr_tms;
    int tdi = _curr_tdi;
#endif

    if (rx)
        memset(rx, 0, (len + 7) / 8);


    for (uint32_t i = 0; i < len; i++) {

#ifdef GPIOD_APIV2

        if (tx) {
            tdi =
                (tx[i >> 3] & (1 << (i & 7))) ?
                GPIOD_LINE_VALUE_ACTIVE :
                GPIOD_LINE_VALUE_INACTIVE;
        }

        gpiod_line_value tms =
            (end && i == len - 1) ?
            GPIOD_LINE_VALUE_ACTIVE :
            _curr_tms;

        update_pins(tms, tdi);

        gpiod_line_request_set_value(
            _tck_request,
            TCK_PIN,
            GPIOD_LINE_VALUE_ACTIVE);

        _curr_tck = 1;

#else

        if (end && i == len - 1)
            tms = 1;

        if (tx)
            tdi =
                (tx[i >> 3] & (1 << (i & 7))) ?
                1 : 0;

        update_pins(0, tms, tdi);
        update_pins(1, tms, tdi);

#endif

        if (rx) {
            if (read_tdo() > 0)
                rx[i >> 3] |= 1 << (i & 7);
        }
    }

    return len;
}


int Stm32mp2GpiodJtagBitbang::toggleClk(
    uint8_t tms,
    uint8_t tdi,
    uint32_t clk_len)
{
    for (uint32_t i = 0; i < clk_len; i++) {

#ifdef GPIOD_APIV2

        update_pins(
            tms ? GPIOD_LINE_VALUE_ACTIVE :
                  GPIOD_LINE_VALUE_INACTIVE,

            tdi ? GPIOD_LINE_VALUE_ACTIVE :
                  GPIOD_LINE_VALUE_INACTIVE);

        gpiod_line_request_set_value(
            _tck_request,
            TCK_PIN,
            GPIOD_LINE_VALUE_ACTIVE);

        _curr_tck = 1;

#else

        update_pins(0, tms, tdi);
        update_pins(1, tms, tdi);

#endif
    }

    return clk_len;
}


bool Stm32mp2GpiodJtagBitbang::writeTMSTDI(
    const uint8_t *tms,
    const uint8_t *tdi,
    uint8_t *tdo,
    uint32_t len)
{
    memset(tdo, 0, (len + 7) / 8);

    for (uint32_t i = 0; i < len; i++) {

#ifdef GPIOD_APIV2

        gpiod_line_value tdix =
            (tdi[i >> 3] & (1 << (i & 7))) ?
            GPIOD_LINE_VALUE_ACTIVE :
            GPIOD_LINE_VALUE_INACTIVE;

        gpiod_line_value tmsx =
            (tms[i >> 3] & (1 << (i & 7))) ?
            GPIOD_LINE_VALUE_ACTIVE :
            GPIOD_LINE_VALUE_INACTIVE;

        update_pins(tmsx, tdix);

        gpiod_line_request_set_value(
            _tck_request,
            TCK_PIN,
            GPIOD_LINE_VALUE_ACTIVE);

        _curr_tck = 1;

#else

        int tdix =
            (tdi[i >> 3] & (1 << (i & 7))) ?
            1 : 0;

        int tmsx =
            (tms[i >> 3] & (1 << (i & 7))) ?
            1 : 0;

        update_pins(0, tmsx, tdix);
        update_pins(1, tmsx, tdix);

#endif

        if (read_tdo() > 0)
            tdo[i >> 3] |= 1 << (i & 7);
    }

    return true;
}
