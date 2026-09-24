#include "vl53l0x.h"

#include "esp_log.h"
#include "esp_err.h"

#include "driver/i2c_master.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "VL53L0X";

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t vl53_device = NULL;

static uint8_t stop_variable = 0;

static uint32_t measurement_timing_budget_us = 33000;


/* ============================================================
 * REGISTRADORES
 * ============================================================ */

#define SYSRANGE_START                         0x00
#define SYSTEM_SEQUENCE_CONFIG                0x01

#define SYSTEM_INTERRUPT_CONFIG_GPIO          0x0A
#define SYSTEM_INTERRUPT_CLEAR                0x0B

#define RESULT_INTERRUPT_STATUS               0x13
#define RESULT_RANGE_STATUS                   0x14

#define RESULT_FINAL_RANGE_MM_HI              0x1E

#define MSRC_CONFIG_CONTROL                   0x60
#define MSRC_CONFIG_TIMEOUT_MACROP            0x46

#define FINAL_RANGE_CONFIG_MIN_COUNT_RATE     0x44

#define PRE_RANGE_CONFIG_VCSEL_PERIOD         0x50
#define PRE_RANGE_CONFIG_TIMEOUT_MACROP_HI    0x51
#define PRE_RANGE_CONFIG_VALID_PHASE_LOW      0x56
#define PRE_RANGE_CONFIG_VALID_PHASE_HIGH     0x57

#define FINAL_RANGE_CONFIG_VCSEL_PERIOD       0x70
#define FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI  0x71
#define FINAL_RANGE_CONFIG_VALID_PHASE_LOW    0x47
#define FINAL_RANGE_CONFIG_VALID_PHASE_HIGH   0x48

#define GLOBAL_CONFIG_VCSEL_WIDTH              0x32

#define ALGO_PHASECAL_CONFIG_TIMEOUT          0x30
#define ALGO_PHASECAL_LIM                     0x30


/* ============================================================
 * I2C - ESCRITA 8 BITS
 * ============================================================ */

static bool vl53_write_reg8(uint8_t reg, uint8_t value)
{
    uint8_t data[2];

    data[0] = reg;
    data[1] = value;

    esp_err_t err = i2c_master_transmit(
        vl53_device,
        data,
        sizeof(data),
        100
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Erro escrevendo 0x%02X: %s",
            reg,
            esp_err_to_name(err)
        );

        return false;
    }

    return true;
}


/* ============================================================
 * I2C - LEITURA 8 BITS
 * ============================================================ */

static bool vl53_read_reg8(uint8_t reg, uint8_t *value)
{
    if (value == NULL)
        return false;

    esp_err_t err = i2c_master_transmit_receive(
        vl53_device,
        &reg,
        1,
        value,
        1,
        100
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Erro lendo 0x%02X: %s",
            reg,
            esp_err_to_name(err)
        );

        return false;
    }

    return true;
}


/* ============================================================
 * I2C - ESCRITA 16 BITS
 * ============================================================ */

static bool vl53_write_reg16(uint8_t reg, uint16_t value)
{
    uint8_t data[3];

    data[0] = reg;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value & 0xFF);

    esp_err_t err = i2c_master_transmit(
        vl53_device,
        data,
        sizeof(data),
        100
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Erro escrevendo 16-bit 0x%02X: %s",
            reg,
            esp_err_to_name(err)
        );

        return false;
    }

    return true;
}


/* ============================================================
 * I2C - LEITURA 16 BITS
 * ============================================================ */

static bool vl53_read_reg16(
    uint8_t reg,
    uint16_t *value)
{
    if (value == NULL)
        return false;

    uint8_t data[2];

    esp_err_t err = i2c_master_transmit_receive(
        vl53_device,
        &reg,
        1,
        data,
        2,
        100
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Erro lendo 16-bit 0x%02X: %s",
            reg,
            esp_err_to_name(err)
        );

        return false;
    }

    *value =
        ((uint16_t)data[0] << 8) |
        data[1];

    return true;
}


/* ============================================================
 * TIMEOUT
 *
 * Mesma representação utilizada pela API de referência.
 * ============================================================ */

static uint16_t vl53_decode_timeout(
    uint16_t reg_val)
{
    uint16_t ls_byte =
        reg_val & 0x00FF;

    uint16_t ms_byte =
        (reg_val >> 8) & 0x00FF;

    return
        (uint16_t)(
            (ls_byte << ms_byte) + 1
        );
}


static uint16_t vl53_encode_timeout(
    uint16_t timeout_mclks)
{
    if (timeout_mclks == 0)
        return 0;

    uint16_t ls_byte =
        timeout_mclks - 1;

    uint16_t ms_byte = 0;

    while ((ls_byte & 0xFFFFFF00) != 0)
    {
        ls_byte >>= 1;
        ms_byte++;
    }

    return
        (uint16_t)(
            (ms_byte << 8) |
            (ls_byte & 0xFF)
        );
}


/* ============================================================
 * MACRO PERIOD
 * ============================================================ */

static uint32_t vl53_calc_macro_period_ns(
    uint8_t vcsel_period_pclks)
{
    return
        (uint32_t)(
            (
                (uint64_t)2304 *
                vcsel_period_pclks *
                1655
            + 500
            ) / 1000
        );
}


/* ============================================================
 * TIMEOUT -> MICROSEGUNDOS
 * ============================================================ */

static uint32_t vl53_timeout_mclks_to_us(
    uint16_t timeout_mclks,
    uint8_t vcsel_period_pclks)
{
    uint32_t macro_period_ns =
        vl53_calc_macro_period_ns(
            vcsel_period_pclks
        );

    return
        (
            (
                (uint32_t)timeout_mclks *
                macro_period_ns
            + macro_period_ns / 2
            ) / 1000
        );
}


/* ============================================================
 * MICROSEGUNDOS -> TIMEOUT
 * ============================================================ */

static uint16_t vl53_timeout_us_to_mclks(
    uint32_t timeout_us,
    uint8_t vcsel_period_pclks)
{
    uint32_t macro_period_ns =
        vl53_calc_macro_period_ns(
            vcsel_period_pclks
        );

    return
        (uint16_t)(
            (
                timeout_us * 1000 +
                macro_period_ns / 2
            ) / macro_period_ns
        );
}


/* ============================================================
 * VCSEL ENCODE
 * ============================================================ */

static uint8_t vl53_encode_vcsel_period(
    uint8_t period_pclks)
{
    return
        (uint8_t)(
            (period_pclks >> 1) - 1
        );
}


/* ============================================================
 * VCSEL DECODE
 * ============================================================ */

static uint8_t vl53_decode_vcsel_period(
    uint8_t reg_value)
{
    return
        (uint8_t)(
            (reg_value + 1) << 1
        );
}


/* ============================================================
 * SEQUENCE ENABLES
 * ============================================================ */

typedef struct
{
    bool tcc;
    bool dss;
    bool msrc;
    bool pre_range;
    bool final_range;

} vl53_sequence_enables_t;


/* ============================================================
 * SEQUENCE TIMEOUTS
 * ============================================================ */

typedef struct
{
    uint16_t msrc_dss_tcc_mclks;

    uint16_t pre_range_mclks;

    uint16_t final_range_mclks;

    uint8_t pre_range_vcsel_period;

    uint8_t final_range_vcsel_period;

    uint32_t msrc_dss_tcc_us;

    uint32_t pre_range_us;

    uint32_t final_range_us;

} vl53_sequence_timeouts_t;


/* ============================================================
 * LER SEQUENCE ENABLES
 * ============================================================ */

static bool vl53_get_sequence_enables(
    vl53_sequence_enables_t *enables)
{
    if (enables == NULL)
        return false;

    uint8_t sequence_config = 0;

    if (!vl53_read_reg8(
            SYSTEM_SEQUENCE_CONFIG,
            &sequence_config))
    {
        return false;
    }

    enables->tcc =
        (sequence_config >> 4) & 0x01;

    enables->dss =
        (sequence_config >> 3) & 0x01;

    enables->msrc =
        (sequence_config >> 2) & 0x01;

    enables->pre_range =
        (sequence_config >> 6) & 0x01;

    enables->final_range =
        (sequence_config >> 7) & 0x01;

    return true;
}


/* ============================================================
 * LER TIMEOUTS
 * ============================================================ */

static bool vl53_get_sequence_timeouts(
    const vl53_sequence_enables_t *enables,
    vl53_sequence_timeouts_t *timeouts)
{
    if (enables == NULL ||
        timeouts == NULL)
    {
        return false;
    }

    uint16_t reg_value = 0;

    uint8_t vcsel = 0;

    /* --------------------------------------------------------
     * MSRC / DSS / TCC
     * -------------------------------------------------------- */

    if (!vl53_read_reg8(
            MSRC_CONFIG_TIMEOUT_MACROP,
            &vcsel))
    {
        return false;
    }

    timeouts->msrc_dss_tcc_mclks =
        (uint16_t)vcsel + 1;


    /* --------------------------------------------------------
     * PRE RANGE VCSEL
     * -------------------------------------------------------- */

    if (!vl53_read_reg8(
            PRE_RANGE_CONFIG_VCSEL_PERIOD,
            &vcsel))
    {
        return false;
    }

    timeouts->pre_range_vcsel_period =
        vl53_decode_vcsel_period(vcsel);


    /* --------------------------------------------------------
     * PRE RANGE TIMEOUT
     * -------------------------------------------------------- */

    if (!vl53_read_reg16(
            PRE_RANGE_CONFIG_TIMEOUT_MACROP_HI,
            &reg_value))
    {
        return false;
    }

    timeouts->pre_range_mclks =
        vl53_decode_timeout(reg_value);

    timeouts->msrc_dss_tcc_us =
        vl53_timeout_mclks_to_us(
            timeouts->msrc_dss_tcc_mclks,
            timeouts->pre_range_vcsel_period
        );

    timeouts->pre_range_us =
        vl53_timeout_mclks_to_us(
            timeouts->pre_range_mclks,
            timeouts->pre_range_vcsel_period
        );


    /* --------------------------------------------------------
     * FINAL RANGE VCSEL
     * -------------------------------------------------------- */

    if (!vl53_read_reg8(
            FINAL_RANGE_CONFIG_VCSEL_PERIOD,
            &vcsel))
    {
        return false;
    }

    timeouts->final_range_vcsel_period =
        vl53_decode_vcsel_period(vcsel);


    /* --------------------------------------------------------
     * FINAL RANGE TIMEOUT
     * -------------------------------------------------------- */

    if (!vl53_read_reg16(
            FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI,
            &reg_value))
    {
        return false;
    }

    timeouts->final_range_mclks =
        vl53_decode_timeout(reg_value);

    timeouts->final_range_us =
        vl53_timeout_mclks_to_us(
            timeouts->final_range_mclks,
            timeouts->final_range_vcsel_period
        );

    return true;
}


/* ============================================================
 * CONFIGURA TIMING BUDGET
 *
 * Baseado na divisão de orçamento da API de referência.
 * ============================================================ */

static bool vl53_set_measurement_timing_budget(
    uint32_t budget_us)
{
    const uint16_t StartOverhead = 1320;
    const uint16_t EndOverhead = 960;
    const uint16_t MsrcOverhead = 660;
    const uint16_t TccOverhead = 590;
    const uint16_t DssOverhead = 690;
    const uint16_t PreRangeOverhead = 660;
    const uint16_t FinalRangeOverhead = 550;

    const uint32_t MinTimingBudget = 20000;

    if (budget_us < MinTimingBudget)
    {
        ESP_LOGE(
            TAG,
            "Timing budget muito baixo: %lu us",
            (unsigned long)budget_us
        );

        return false;
    }

    vl53_sequence_enables_t enables;

    vl53_sequence_timeouts_t timeouts;

    if (!vl53_get_sequence_enables(
            &enables))
    {
        return false;
    }

    if (!vl53_get_sequence_timeouts(
            &enables,
            &timeouts))
    {
        return false;
    }

    uint32_t used_budget_us =
        StartOverhead +
        EndOverhead;


    if (enables.tcc)
    {
        used_budget_us +=
            timeouts.msrc_dss_tcc_us +
            TccOverhead;
    }


    if (enables.dss)
    {
        used_budget_us +=
            2 *
            (
                timeouts.msrc_dss_tcc_us +
                DssOverhead
            );
    }
    else if (enables.msrc)
    {
        used_budget_us +=
            timeouts.msrc_dss_tcc_us +
            MsrcOverhead;
    }


    if (enables.pre_range)
    {
        used_budget_us +=
            timeouts.pre_range_us +
            PreRangeOverhead;
    }


    if (!enables.final_range)
    {
        ESP_LOGE(
            TAG,
            "Final Range desabilitado"
        );

        return false;
    }


    used_budget_us +=
        FinalRangeOverhead;


    if (used_budget_us >= budget_us)
    {
        ESP_LOGE(
            TAG,
            "Timing budget insuficiente. Usado=%lu us Budget=%lu us",
            (unsigned long)used_budget_us,
            (unsigned long)budget_us
        );

        return false;
    }


    uint32_t final_range_timeout_us =
        budget_us -
        used_budget_us;


    /*
     * O timeout final inclui o timeout do Pre-Range.
     */

    if (enables.pre_range)
    {
        final_range_timeout_us +=
            timeouts.pre_range_us;
    }


    uint16_t final_range_timeout_mclks =
        vl53_timeout_us_to_mclks(
            final_range_timeout_us,
            timeouts.final_range_vcsel_period
        );


    if (enables.pre_range)
    {
        final_range_timeout_mclks +=
            timeouts.pre_range_mclks;
    }


    uint16_t encoded =
        vl53_encode_timeout(
            final_range_timeout_mclks
        );


    if (!vl53_write_reg16(
            FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI,
            encoded))
    {
        return false;
    }


    measurement_timing_budget_us =
        budget_us;


    ESP_LOGI(
        TAG,
        "Timing budget configurado: %lu us",
        (unsigned long)budget_us
    );

    return true;
}


/* ============================================================
 * CONFIGURA VCSEL PRE-RANGE
 * ============================================================ */

static bool vl53_set_pre_range_vcsel(
    uint8_t period_pclks)
{
    if (period_pclks < 12 ||
        period_pclks > 18 ||
        (period_pclks & 1))
    {
        ESP_LOGE(
            TAG,
            "Pre-Range VCSEL invalido: %u",
            period_pclks
        );

        return false;
    }


    vl53_sequence_enables_t enables;

    vl53_sequence_timeouts_t timeouts;

    if (!vl53_get_sequence_enables(
            &enables))
    {
        return false;
    }

    if (!vl53_get_sequence_timeouts(
            &enables,
            &timeouts))
    {
        return false;
    }


    /* --------------------------------------------------------
     * VALID PHASE
     * -------------------------------------------------------- */

    uint8_t phase_high = 0;

    switch (period_pclks)
    {
        case 12:
            phase_high = 0x18;
            break;

        case 14:
            phase_high = 0x30;
            break;

        case 16:
            phase_high = 0x40;
            break;

        case 18:
            phase_high = 0x50;
            break;

        default:
            return false;
    }


    if (!vl53_write_reg8(
            PRE_RANGE_CONFIG_VALID_PHASE_HIGH,
            phase_high))
    {
        return false;
    }

    if (!vl53_write_reg8(
            PRE_RANGE_CONFIG_VALID_PHASE_LOW,
            0x08))
    {
        return false;
    }


    /* --------------------------------------------------------
     * VCSEL
     * -------------------------------------------------------- */

    if (!vl53_write_reg8(
            PRE_RANGE_CONFIG_VCSEL_PERIOD,
            vl53_encode_vcsel_period(
                period_pclks)))
    {
        return false;
    }


    /* --------------------------------------------------------
     * RECALCULA PRE RANGE TIMEOUT
     * -------------------------------------------------------- */

    uint16_t new_pre_timeout =
        vl53_timeout_us_to_mclks(
            timeouts.pre_range_us,
            period_pclks
        );


    if (!vl53_write_reg16(
            PRE_RANGE_CONFIG_TIMEOUT_MACROP_HI,
            vl53_encode_timeout(
                new_pre_timeout)))
    {
        return false;
    }


    /* --------------------------------------------------------
     * RECALCULA MSRC TIMEOUT
     * -------------------------------------------------------- */

    uint16_t new_msrc_timeout =
        vl53_timeout_us_to_mclks(
            timeouts.msrc_dss_tcc_us,
            period_pclks
        );


    if (new_msrc_timeout > 256)
        new_msrc_timeout = 256;


    if (!vl53_write_reg8(
            MSRC_CONFIG_TIMEOUT_MACROP,
            (uint8_t)(
                new_msrc_timeout - 1
            )))
    {
        return false;
    }


    ESP_LOGI(
        TAG,
        "Pre-Range VCSEL: %u PCLK",
        period_pclks
    );

    return true;
}


/* ============================================================
 * CONFIGURA VCSEL FINAL-RANGE
 * ============================================================ */

static bool vl53_set_final_range_vcsel(
    uint8_t period_pclks)
{
    if (period_pclks < 8 ||
        period_pclks > 14 ||
        (period_pclks & 1))
    {
        ESP_LOGE(
            TAG,
            "Final-Range VCSEL invalido: %u",
            period_pclks
        );

        return false;
    }


    vl53_sequence_enables_t enables;

    vl53_sequence_timeouts_t timeouts;

    if (!vl53_get_sequence_enables(
            &enables))
    {
        return false;
    }

    if (!vl53_get_sequence_timeouts(
            &enables,
            &timeouts))
    {
        return false;
    }


    /* --------------------------------------------------------
     * VALID PHASE / WIDTH
     * -------------------------------------------------------- */

    uint8_t phase_high = 0;
    uint8_t vcsel_width = 0;
    uint8_t phasecal_timeout = 0;


    switch (period_pclks)
    {
        case 8:

            phase_high = 0x10;
            vcsel_width = 0x02;
            phasecal_timeout = 0x0C;

            break;


        case 10:

            phase_high = 0x28;
            vcsel_width = 0x03;
            phasecal_timeout = 0x09;

            break;


        case 12:

            phase_high = 0x38;
            vcsel_width = 0x03;
            phasecal_timeout = 0x08;

            break;


        case 14:

            phase_high = 0x48;
            vcsel_width = 0x03;
            phasecal_timeout = 0x07;

            break;


        default:
            return false;
    }


    if (!vl53_write_reg8(
            FINAL_RANGE_CONFIG_VALID_PHASE_HIGH,
            phase_high))
    {
        return false;
    }

    if (!vl53_write_reg8(
            FINAL_RANGE_CONFIG_VALID_PHASE_LOW,
            0x08))
    {
        return false;
    }


    if (!vl53_write_reg8(
            GLOBAL_CONFIG_VCSEL_WIDTH,
            vcsel_width))
    {
        return false;
    }


    if (!vl53_write_reg8(
            ALGO_PHASECAL_CONFIG_TIMEOUT,
            phasecal_timeout))
    {
        return false;
    }


    /* --------------------------------------------------------
     * VCSEL
     * -------------------------------------------------------- */

    if (!vl53_write_reg8(
            FINAL_RANGE_CONFIG_VCSEL_PERIOD,
            vl53_encode_vcsel_period(
                period_pclks)))
    {
        return false;
    }


    /* --------------------------------------------------------
     * RECALCULA FINAL RANGE TIMEOUT
     * -------------------------------------------------------- */

    uint16_t new_final_timeout =
        vl53_timeout_us_to_mclks(
            timeouts.final_range_us,
            period_pclks
        );


    if (enables.pre_range)
    {
        new_final_timeout +=
            timeouts.pre_range_mclks;
    }


    if (!vl53_write_reg16(
            FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI,
            vl53_encode_timeout(
                new_final_timeout)))
    {
        return false;
    }


    ESP_LOGI(
        TAG,
        "Final-Range VCSEL: %u PCLK",
        period_pclks
    );

    return true;
}


/* ============================================================
 * CALIBRACAO DE FASE
 * ============================================================ */

static bool vl53_perform_single_ref_calibration(
    uint8_t vhv_init_byte)
{
    if (!vl53_write_reg8(
            SYSRANGE_START,
            0x01 | vhv_init_byte))
    {
        return false;
    }


    uint8_t status = 0;

    int tentativas = 0;


    while (tentativas < 200)
    {
        if (!vl53_read_reg8(
                RESULT_INTERRUPT_STATUS,
                &status))
        {
            return false;
        }


        if ((status & 0x07) != 0)
            break;


        vTaskDelay(
            pdMS_TO_TICKS(5)
        );

        tentativas++;
    }


    if (tentativas >= 200)
    {
        ESP_LOGE(
            TAG,
            "Timeout na calibracao"
        );

        return false;
    }


    if (!vl53_write_reg8(
            SYSTEM_INTERRUPT_CLEAR,
            0x01))
    {
        return false;
    }


    if (!vl53_write_reg8(
            SYSRANGE_START,
            0x00))
    {
        return false;
    }


    return true;
}


/* ============================================================
 * CALIBRACAO DE FASE APOS VCSEL
 * ============================================================ */

static bool vl53_perform_phase_calibration(void)
{
    uint8_t sequence_config = 0;

    if (!vl53_read_reg8(
            SYSTEM_SEQUENCE_CONFIG,
            &sequence_config))
    {
        return false;
    }


    if (!vl53_write_reg8(
            SYSTEM_SEQUENCE_CONFIG,
            0x02))
    {
        return false;
    }


    bool result =
        vl53_perform_single_ref_calibration(
            0x00
        );


    if (!vl53_write_reg8(
            SYSTEM_SEQUENCE_CONFIG,
            sequence_config))
    {
        return false;
    }


    if (!result)
    {
        ESP_LOGE(
            TAG,
            "Falha na calibracao de fase"
        );

        return false;
    }


    ESP_LOGI(
        TAG,
        "Calibracao de fase concluida"
    );

    return true;
}


/* ============================================================
 * SPAD INFO
 * ============================================================ */

static bool vl53_get_spad_info(
    uint8_t *count,
    bool *is_aperture)
{
    if (count == NULL ||
        is_aperture == NULL)
    {
        return false;
    }


    if (!vl53_write_reg8(
            0x80,
            0x01))
        return false;

    if (!vl53_write_reg8(
            0xFF,
            0x01))
        return false;

    if (!vl53_write_reg8(
            0x00,
            0x00))
        return false;

    if (!vl53_write_reg8(
            0xFF,
            0x06))
        return false;


    uint8_t value = 0;


    if (!vl53_read_reg8(
            0x83,
            &value))
        return false;


    if (!vl53_write_reg8(
            0x83,
            value | 0x04))
        return false;


    if (!vl53_write_reg8(
            0xFF,
            0x07))
        return false;

    if (!vl53_write_reg8(
            0x81,
            0x01))
        return false;

    if (!vl53_write_reg8(
            0x80,
            0x01))
        return false;

    if (!vl53_write_reg8(
            0x94,
            0x6B))
        return false;

    if (!vl53_write_reg8(
            0x83,
            0x00))
        return false;


    int tentativas = 0;


    while (tentativas < 200)
    {
        if (!vl53_read_reg8(
                0x83,
                &value))
        {
            return false;
        }


        if (value != 0)
            break;


        vTaskDelay(
            pdMS_TO_TICKS(1)
        );

        tentativas++;
    }


    if (tentativas >= 200)
    {
        ESP_LOGE(
            TAG,
            "Timeout obtendo SPAD info"
        );

        return false;
    }


    if (!vl53_read_reg8(
            0x92,
            &value))
    {
        return false;
    }


    *count =
        value & 0x7F;


    *is_aperture =
        (value & 0x80) != 0;


    if (!vl53_write_reg8(
            0x83,
            0x01))
        return false;

    if (!vl53_write_reg8(
            0x81,
            0x00))
        return false;

    if (!vl53_write_reg8(
            0xFF,
            0x06))
        return false;


    if (!vl53_read_reg8(
            0x83,
            &value))
    {
        return false;
    }


    if (!vl53_write_reg8(
            0x83,
            value & ~0x04))
    {
        return false;
    }


    if (!vl53_write_reg8(
            0xFF,
            0x01))
        return false;

    if (!vl53_write_reg8(
            0x00,
            0x01))
        return false;

    if (!vl53_write_reg8(
            0xFF,
            0x00))
        return false;

    if (!vl53_write_reg8(
            0x80,
            0x00))
        return false;


    ESP_LOGI(
        TAG,
        "SPAD info: quantidade=%u | aperture=%s",
        *count,
        *is_aperture ? "SIM" : "NAO"
    );


    return true;
}


/* ============================================================
 * REFERENCE SPAD MANAGEMENT
 * ============================================================ */

static bool vl53_perform_ref_spad_management(void)
{
    uint8_t spad_count = 0;

    bool is_aperture = false;


    if (!vl53_get_spad_info(
            &spad_count,
            &is_aperture))
    {
        return false;
    }


    if (spad_count == 0 ||
        spad_count > 44)
    {
        ESP_LOGE(
            TAG,
            "SPAD count invalido: %u",
            spad_count
        );

        return false;
    }


    uint8_t spad_map[6];


    for (int i = 0; i < 6; i++)
    {
        if (!vl53_read_reg8(
                0xB0 + i,
                &spad_map[i]))
        {
            return false;
        }
    }


    if (!vl53_write_reg8(
            0xFF,
            0x01))
        return false;

    if (!vl53_write_reg8(
            0x4F,
            0x00))
        return false;

    if (!vl53_write_reg8(
            0x4E,
            0x2C))
        return false;

    if (!vl53_write_reg8(
            0xFF,
            0x00))
        return false;

    if (!vl53_write_reg8(
            0xB6,
            0xB4))
        return false;


    uint8_t enabled_spads = 0;


    for (int i = 0; i < 48; i++)
    {
        bool disable = false;


        if (
            (i < 12 && is_aperture) ||
            (enabled_spads >= spad_count)
        )
        {
            disable = true;
        }


        if (disable)
        {
            spad_map[i / 8] &=
                (uint8_t)~(
                    1U << (i % 8)
                );
        }
        else if (
            spad_map[i / 8] &
            (1U << (i % 8))
        )
        {
            enabled_spads++;
        }
    }


    for (int i = 0; i < 6; i++)
    {
        if (!vl53_write_reg8(
                0xB0 + i,
                spad_map[i]))
        {
            return false;
        }
    }


    if (!vl53_write_reg8(
            0xFF,
            0x01))
        return false;

    if (!vl53_write_reg8(
            0x00,
            0x01))
        return false;

    if (!vl53_write_reg8(
            0xFF,
            0x00))
        return false;

    if (!vl53_write_reg8(
            0x80,
            0x00))
        return false;


    ESP_LOGI(
        TAG,
        "Reference SPADs configurados: %u",
        enabled_spads
    );


    return true;
}


/* ============================================================
 * DIAGNOSTICO - LEITURA DA CONFIGURACAO REAL
 *
 * Lê novamente os registradores depois de toda a configuração.
 * Assim verificamos o que realmente ficou gravado no VL53L0X.
 * ============================================================ */

static bool vl53_readback_configuration(void)
{
    uint16_t signal_rate = 0;
    uint8_t pre_vcsel_reg = 0;
    uint8_t final_vcsel_reg = 0;
    uint16_t final_timeout = 0;
    uint8_t sequence_config = 0;
    uint8_t stop_var_check = 0;

    /* --------------------------------------------------------
     * SIGNAL RATE
     * -------------------------------------------------------- */

    if (!vl53_read_reg16(
            FINAL_RANGE_CONFIG_MIN_COUNT_RATE,
            &signal_rate))
    {
        return false;
    }

    float signal_rate_mcps =
        signal_rate / 128.0f;


    /* --------------------------------------------------------
     * PRE-RANGE VCSEL
     * -------------------------------------------------------- */

    if (!vl53_read_reg8(
            PRE_RANGE_CONFIG_VCSEL_PERIOD,
            &pre_vcsel_reg))
    {
        return false;
    }

    uint8_t pre_vcsel =
        vl53_decode_vcsel_period(
            pre_vcsel_reg
        );


    /* --------------------------------------------------------
     * FINAL-RANGE VCSEL
     * -------------------------------------------------------- */

    if (!vl53_read_reg8(
            FINAL_RANGE_CONFIG_VCSEL_PERIOD,
            &final_vcsel_reg))
    {
        return false;
    }

    uint8_t final_vcsel =
        vl53_decode_vcsel_period(
            final_vcsel_reg
        );


    /* --------------------------------------------------------
     * FINAL-RANGE TIMEOUT
     * -------------------------------------------------------- */

    if (!vl53_read_reg16(
            FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI,
            &final_timeout))
    {
        return false;
    }


    /* --------------------------------------------------------
     * SEQUENCE CONFIG
     * -------------------------------------------------------- */

    if (!vl53_read_reg8(
            SYSTEM_SEQUENCE_CONFIG,
            &sequence_config))
    {
        return false;
    }


    /* --------------------------------------------------------
     * STOP VARIABLE
     * -------------------------------------------------------- */

    if (!vl53_write_reg8(
            0x80,
            0x01))
        return false;

    if (!vl53_write_reg8(
            0xFF,
            0x01))
        return false;

    if (!vl53_write_reg8(
            0x00,
            0x00))
        return false;

    if (!vl53_read_reg8(
            0x91,
            &stop_var_check))
    {
        return false;
    }

    if (!vl53_write_reg8(
            0x00,
            0x01))
        return false;

    if (!vl53_write_reg8(
            0xFF,
            0x00))
        return false;

    if (!vl53_write_reg8(
            0x80,
            0x00))
        return false;


    /* --------------------------------------------------------
     * LOG
     * -------------------------------------------------------- */

    ESP_LOGI(
        TAG,
        "========================================"
    );

    ESP_LOGI(
        TAG,
        "READBACK DA CONFIGURACAO REAL"
    );

    ESP_LOGI(
        TAG,
        "Signal Rate:"
    );

    ESP_LOGI(
        TAG,
        "  Registro: 0x%04X",
        signal_rate
    );

    ESP_LOGI(
        TAG,
        "  Valor: %.3f MCPS",
        signal_rate_mcps
    );

    ESP_LOGI(
        TAG,
        "Pre-Range VCSEL:"
    );

    ESP_LOGI(
        TAG,
        "  Registro: 0x%02X",
        pre_vcsel_reg
    );

    ESP_LOGI(
        TAG,
        "  Valor: %u PCLK",
        pre_vcsel
    );

    ESP_LOGI(
        TAG,
        "Final-Range VCSEL:"
    );

    ESP_LOGI(
        TAG,
        "  Registro: 0x%02X",
        final_vcsel_reg
    );

    ESP_LOGI(
        TAG,
        "  Valor: %u PCLK",
        final_vcsel
    );

    ESP_LOGI(
        TAG,
        "Final-Range Timeout:"
    );

    ESP_LOGI(
        TAG,
        "  Registro: 0x%04X",
        final_timeout
    );

    ESP_LOGI(
        TAG,
        "Sequence Config:"
    );

    ESP_LOGI(
        TAG,
        "  Registro: 0x%02X",
        sequence_config
    );

    ESP_LOGI(
        TAG,
        "Stop Variable:"
    );

    ESP_LOGI(
        TAG,
        "  Inicial: 0x%02X",
        stop_variable
    );

    ESP_LOGI(
        TAG,
        "  Atual:   0x%02X",
        stop_var_check
    );

    ESP_LOGI(
        TAG,
        "Timing Budget configurado pelo driver: %lu us",
        (unsigned long)measurement_timing_budget_us
    );

    ESP_LOGI(
        TAG,
        "========================================"
    );


    return true;
}


/* ============================================================
 * LONG RANGE
 *
 * Perfil baseado na configuração documentada pela ST:
 *
 * Signal rate : 0.10 MCPS
 * Sigma       : 60 mm
 * Budget      : 33 ms
 * Pre VCSEL   : 18 PCLK
 * Final VCSEL : 14 PCLK
 * ============================================================ */

static bool vl53_configure_long_range(void)
{
    ESP_LOGI(
        TAG,
        "========================================"
    );

    ESP_LOGI(
        TAG,
        "Configurando perfil LONG RANGE..."
    );


    /* --------------------------------------------------------
     * SIGNAL RATE
     *
     * Q9.7
     *
     * 0.10 * 128 = 12.8
     * aproximadamente 13 = 0x000D
     * -------------------------------------------------------- */

    if (!vl53_write_reg16(
            FINAL_RANGE_CONFIG_MIN_COUNT_RATE,
            0x000D))
    {
        return false;
    }


    ESP_LOGI(
        TAG,
        "Signal Rate Limit: 0.10 MCPS"
    );


    /* --------------------------------------------------------
     * VCSEL PRE-RANGE = 18
     * -------------------------------------------------------- */

    if (!vl53_set_pre_range_vcsel(
            18))
    {
        ESP_LOGE(
            TAG,
            "Falha configurando Pre-Range VCSEL"
        );

        return false;
    }


    /* --------------------------------------------------------
     * VCSEL FINAL-RANGE = 14
     * -------------------------------------------------------- */

    if (!vl53_set_final_range_vcsel(
            14))
    {
        ESP_LOGE(
            TAG,
            "Falha configurando Final-Range VCSEL"
        );

        return false;
    }


    /* --------------------------------------------------------
     * TIMING BUDGET = 33 ms
     * -------------------------------------------------------- */

    if (!vl53_set_measurement_timing_budget(
            33000))
    {
        ESP_LOGE(
            TAG,
            "Falha configurando timing budget"
        );

        return false;
    }


    /* --------------------------------------------------------
     * CALIBRACAO DE FASE
     *
     * Necessária depois da alteração dos VCSELs.
     * -------------------------------------------------------- */

    if (!vl53_perform_phase_calibration())
    {
        ESP_LOGE(
            TAG,
            "Falha na calibracao de fase"
        );

        return false;
    }


    /* --------------------------------------------------------
     * READBACK DA CONFIGURACAO
     * -------------------------------------------------------- */

    if (!vl53_readback_configuration())
    {
        ESP_LOGE(
            TAG,
            "Falha lendo configuracao de volta"
        );

        return false;
    }


    ESP_LOGI(
        TAG,
        "Perfil LONG RANGE configurado"
    );

    ESP_LOGI(
        TAG,
        "Signal Rate : 0.10 MCPS"
    );

    ESP_LOGI(
        TAG,
        "Timing      : 33 ms"
    );

    ESP_LOGI(
        TAG,
        "Pre VCSEL   : 18 PCLK"
    );

    ESP_LOGI(
        TAG,
        "Final VCSEL : 14 PCLK"
    );

    ESP_LOGI(
        TAG,
        "========================================"
    );


    return true;
}


/* ============================================================
 * INICIALIZACAO
 * ============================================================ */

bool vl53l0x_init(void)
{
    ESP_LOGI(
        TAG,
        "========================================"
    );

    ESP_LOGI(
        TAG,
        "Inicializando VL53L0X..."
    );

    ESP_LOGI(
        TAG,
        "SDA: GPIO %d",
        VL53L0X_SDA_GPIO
    );

    ESP_LOGI(
        TAG,
        "SCL: GPIO %d",
        VL53L0X_SCL_GPIO
    );

    ESP_LOGI(
        TAG,
        "Endereco I2C: 0x%02X",
        VL53L0X_I2C_ADDRESS
    );


    /* --------------------------------------------------------
     * I2C BUS
     * -------------------------------------------------------- */

    i2c_master_bus_config_t bus_config = {
        .i2c_port = VL53L0X_I2C_PORT,
        .sda_io_num = VL53L0X_SDA_GPIO,
        .scl_io_num = VL53L0X_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };


    esp_err_t err =
        i2c_new_master_bus(
            &bus_config,
            &i2c_bus
        );


    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Falha criando I2C: %s",
            esp_err_to_name(err)
        );

        return false;
    }


    /* --------------------------------------------------------
     * DEVICE
     * -------------------------------------------------------- */

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = VL53L0X_I2C_ADDRESS,
        .scl_speed_hz = 400000
    };


    err =
        i2c_master_bus_add_device(
            i2c_bus,
            &device_config,
            &vl53_device
        );


    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Falha adicionando VL53L0X: %s",
            esp_err_to_name(err)
        );

        return false;
    }


    /* --------------------------------------------------------
     * PROBE
     * -------------------------------------------------------- */

    err =
        i2c_master_probe(
            i2c_bus,
            VL53L0X_I2C_ADDRESS,
            100
        );


    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "VL53L0X nao respondeu em 0x%02X",
            VL53L0X_I2C_ADDRESS
        );

        return false;
    }


    ESP_LOGI(
        TAG,
        "VL53L0X respondeu em 0x%02X",
        VL53L0X_I2C_ADDRESS
    );


    /* --------------------------------------------------------
     * MODEL ID
     * -------------------------------------------------------- */

    uint8_t model_id = 0;


    if (!vl53_read_reg8(
            0xC0,
            &model_id))
    {
        return false;
    }


    ESP_LOGI(
        TAG,
        "Model ID: 0x%02X",
        model_id
    );


    if (model_id != 0xEE)
    {
        ESP_LOGE(
            TAG,
            "Model ID inesperado"
        );

        return false;
    }


    ESP_LOGI(
        TAG,
        "VL53L0X identificado corretamente!"
    );


    /* --------------------------------------------------------
     * HIGH VOLTAGE / STOP VARIABLE
     * -------------------------------------------------------- */

    uint8_t ext_hv = 0;


    if (!vl53_read_reg8(
            0x89,
            &ext_hv))
    {
        return false;
    }


    if (!vl53_write_reg8(
            0x89,
            ext_hv | 0x01))
    {
        return false;
    }


    if (!vl53_write_reg8(
            0x88,
            0x00))
        return false;


    if (!vl53_write_reg8(
            0x80,
            0x01))
        return false;


    if (!vl53_write_reg8(
            0xFF,
            0x01))
        return false;


    if (!vl53_write_reg8(
            0x00,
            0x00))
        return false;


    if (!vl53_read_reg8(
            0x91,
            &stop_variable))
    {
        return false;
    }


    ESP_LOGI(
        TAG,
        "Stop variable: 0x%02X",
        stop_variable
    );


    if (!vl53_write_reg8(
            0x00,
            0x01))
        return false;


    if (!vl53_write_reg8(
            0xFF,
            0x00))
        return false;


    if (!vl53_write_reg8(
            0x80,
            0x00))
        return false;


    /* --------------------------------------------------------
     * MSRC
     * -------------------------------------------------------- */

    uint8_t msrc_control = 0;


    if (!vl53_read_reg8(
            MSRC_CONFIG_CONTROL,
            &msrc_control))
    {
        return false;
    }


    if (!vl53_write_reg8(
            MSRC_CONFIG_CONTROL,
            msrc_control | 0x12))
    {
        return false;
    }


    /* --------------------------------------------------------
     * SIGNAL RATE INICIAL
     *
     * Depois o Long Range aplica 0.10 MCPS.
     * -------------------------------------------------------- */

    if (!vl53_write_reg16(
            FINAL_RANGE_CONFIG_MIN_COUNT_RATE,
            0x0020))
    {
        return false;
    }


    /* --------------------------------------------------------
     * REFERENCE SPAD
     * -------------------------------------------------------- */

    ESP_LOGI(
        TAG,
        "Iniciando Reference SPAD Management..."
    );


    if (!vl53_perform_ref_spad_management())
    {
        ESP_LOGE(
            TAG,
            "Falha no Reference SPAD Management"
        );

        return false;
    }


    ESP_LOGI(
        TAG,
        "Reference SPAD Management concluido"
    );


    /* --------------------------------------------------------
     * TUNING SETTINGS
     * -------------------------------------------------------- */

    if (!vl53_write_reg8(0xFF, 0x01)) return false;
    if (!vl53_write_reg8(0x00, 0x00)) return false;
    if (!vl53_write_reg8(0xFF, 0x00)) return false;

    if (!vl53_write_reg8(0x09, 0x00)) return false;
    if (!vl53_write_reg8(0x10, 0x00)) return false;
    if (!vl53_write_reg8(0x11, 0x00)) return false;
    if (!vl53_write_reg8(0x24, 0x01)) return false;
    if (!vl53_write_reg8(0x25, 0xFF)) return false;
    if (!vl53_write_reg8(0x75, 0x00)) return false;

    if (!vl53_write_reg8(0xFF, 0x01)) return false;
    if (!vl53_write_reg8(0x4E, 0x2C)) return false;
    if (!vl53_write_reg8(0x48, 0x00)) return false;
    if (!vl53_write_reg8(0x30, 0x20)) return false;

    if (!vl53_write_reg8(0xFF, 0x00)) return false;
    if (!vl53_write_reg8(0x30, 0x09)) return false;
    if (!vl53_write_reg8(0x54, 0x00)) return false;
    if (!vl53_write_reg8(0x31, 0x04)) return false;
    if (!vl53_write_reg8(0x32, 0x03)) return false;
    if (!vl53_write_reg8(0x40, 0x83)) return false;
    if (!vl53_write_reg8(0x46, 0x25)) return false;
    if (!vl53_write_reg8(0x60, 0x00)) return false;
    if (!vl53_write_reg8(0x27, 0x00)) return false;
    if (!vl53_write_reg8(0x50, 0x06)) return false;
    if (!vl53_write_reg8(0x51, 0x00)) return false;
    if (!vl53_write_reg8(0x52, 0x96)) return false;
    if (!vl53_write_reg8(0x56, 0x08)) return false;
    if (!vl53_write_reg8(0x57, 0x30)) return false;
    if (!vl53_write_reg8(0x61, 0x00)) return false;
    if (!vl53_write_reg8(0x62, 0x00)) return false;
    if (!vl53_write_reg8(0x64, 0x00)) return false;
    if (!vl53_write_reg8(0x65, 0x00)) return false;
    if (!vl53_write_reg8(0x66, 0xA0)) return false;

    if (!vl53_write_reg8(0xFF, 0x01)) return false;
    if (!vl53_write_reg8(0x22, 0x32)) return false;
    if (!vl53_write_reg8(0x47, 0x14)) return false;
    if (!vl53_write_reg8(0x49, 0xFF)) return false;
    if (!vl53_write_reg8(0x4A, 0x00)) return false;

    if (!vl53_write_reg8(0xFF, 0x00)) return false;
    if (!vl53_write_reg8(0x7A, 0x0A)) return false;
    if (!vl53_write_reg8(0x7B, 0x00)) return false;
    if (!vl53_write_reg8(0x78, 0x21)) return false;

    if (!vl53_write_reg8(0xFF, 0x01)) return false;
    if (!vl53_write_reg8(0x23, 0x34)) return false;
    if (!vl53_write_reg8(0x42, 0x00)) return false;
    if (!vl53_write_reg8(0x44, 0xFF)) return false;
    if (!vl53_write_reg8(0x45, 0x26)) return false;
    if (!vl53_write_reg8(0x46, 0x05)) return false;
    if (!vl53_write_reg8(0x40, 0x40)) return false;
    if (!vl53_write_reg8(0x0E, 0x06)) return false;
    if (!vl53_write_reg8(0x20, 0x1A)) return false;
    if (!vl53_write_reg8(0x43, 0x40)) return false;

    if (!vl53_write_reg8(0xFF, 0x00)) return false;
    if (!vl53_write_reg8(0x34, 0x03)) return false;
    if (!vl53_write_reg8(0x35, 0x44)) return false;

    if (!vl53_write_reg8(0xFF, 0x01)) return false;
    if (!vl53_write_reg8(0x31, 0x04)) return false;
    if (!vl53_write_reg8(0x4B, 0x09)) return false;
    if (!vl53_write_reg8(0x4C, 0x05)) return false;
    if (!vl53_write_reg8(0x4D, 0x04)) return false;

    if (!vl53_write_reg8(0xFF, 0x00)) return false;
    if (!vl53_write_reg8(0x44, 0x00)) return false;
    if (!vl53_write_reg8(0x45, 0x20)) return false;
    if (!vl53_write_reg8(0x47, 0x08)) return false;
    if (!vl53_write_reg8(0x48, 0x28)) return false;
    if (!vl53_write_reg8(0x67, 0x00)) return false;
    if (!vl53_write_reg8(0x70, 0x04)) return false;
    if (!vl53_write_reg8(0x71, 0x01)) return false;
    if (!vl53_write_reg8(0x72, 0xFE)) return false;
    if (!vl53_write_reg8(0x76, 0x00)) return false;
    if (!vl53_write_reg8(0x77, 0x00)) return false;

    if (!vl53_write_reg8(0xFF, 0x01)) return false;
    if (!vl53_write_reg8(0x0D, 0x01)) return false;

    if (!vl53_write_reg8(0xFF, 0x00)) return false;
    if (!vl53_write_reg8(0x80, 0x01)) return false;
    if (!vl53_write_reg8(0x01, 0xF8)) return false;

    if (!vl53_write_reg8(0xFF, 0x01)) return false;
    if (!vl53_write_reg8(0x8E, 0x01)) return false;
    if (!vl53_write_reg8(0x00, 0x01)) return false;

    if (!vl53_write_reg8(0xFF, 0x00)) return false;
    if (!vl53_write_reg8(0x80, 0x00)) return false;


    /* --------------------------------------------------------
     * INTERRUPT
     * -------------------------------------------------------- */

    if (!vl53_write_reg8(
            SYSTEM_INTERRUPT_CONFIG_GPIO,
            0x04))
    {
        return false;
    }


    uint8_t gpio_mux = 0;


    if (!vl53_read_reg8(
            0x84,
            &gpio_mux))
    {
        return false;
    }


    if (!vl53_write_reg8(
            0x84,
            gpio_mux & ~0x10))
    {
        return false;
    }


    if (!vl53_write_reg8(
            SYSTEM_INTERRUPT_CLEAR,
            0x01))
    {
        return false;
    }


    /* --------------------------------------------------------
     * SEQUENCE DEFAULT
     * -------------------------------------------------------- */

    if (!vl53_write_reg8(
            SYSTEM_SEQUENCE_CONFIG,
            0xE8))
    {
        return false;
    }


    /* --------------------------------------------------------
     * VHV CALIBRATION
     * -------------------------------------------------------- */

    ESP_LOGI(
        TAG,
        "Iniciando calibracao VHV..."
    );


    if (!vl53_write_reg8(
            SYSTEM_SEQUENCE_CONFIG,
            0x01))
    {
        return false;
    }


    if (!vl53_perform_single_ref_calibration(
            0x40))
    {
        ESP_LOGE(
            TAG,
            "Falha calibracao VHV"
        );

        return false;
    }


    ESP_LOGI(
        TAG,
        "Calibracao VHV concluida"
    );


    /* --------------------------------------------------------
     * RESTAURA SEQUENCE
     * -------------------------------------------------------- */

    if (!vl53_write_reg8(
            SYSTEM_SEQUENCE_CONFIG,
            0xE8))
    {
        return false;
    }


    /* --------------------------------------------------------
     * LONG RANGE
     * -------------------------------------------------------- */


        



    if (!vl53_configure_long_range())
    {
        ESP_LOGE(
            TAG,
            "Falha configurando LONG RANGE"
        );

        return false;
    }



    /* --------------------------------------------------------
     * CALIBRACAO FINAL
     * -------------------------------------------------------- */

    if (!vl53_write_reg8(
            SYSTEM_INTERRUPT_CLEAR,
            0x01))
    {
        return false;
    }


    ESP_LOGI(
        TAG,
        "VL53L0X inicializado!"
    );

    ESP_LOGI(
        TAG,
        "Modo: LONG RANGE"
    );

    ESP_LOGI(
        TAG,
        "========================================"
    );


    return true;
}


/* ============================================================
 * LEITURA SINGLE RANGING
 * ============================================================ */

bool vl53l0x_read_distance(
    uint16_t *distance_mm)
{
    if (distance_mm == NULL)
        return false;


    if (vl53_device == NULL)
    {
        ESP_LOGE(
            TAG,
            "Sensor nao inicializado!"
        );

        return false;
    }


    /* --------------------------------------------------------
     * PREPARA STOP VARIABLE
     * -------------------------------------------------------- */

    if (!vl53_write_reg8(
            0x80,
            0x01))
        return false;


    if (!vl53_write_reg8(
            0xFF,
            0x01))
        return false;


    if (!vl53_write_reg8(
            0x00,
            0x00))
        return false;


    if (!vl53_write_reg8(
            0x91,
            stop_variable))
        return false;


    if (!vl53_write_reg8(
            0x00,
            0x01))
        return false;


    if (!vl53_write_reg8(
            0xFF,
            0x00))
        return false;


    if (!vl53_write_reg8(
            0x80,
            0x00))
        return false;


    /* --------------------------------------------------------
     * START SINGLE RANGING
     * -------------------------------------------------------- */

    if (!vl53_write_reg8(
            SYSRANGE_START,
            0x01))
    {
        return false;
    }


    /* --------------------------------------------------------
     * AGUARDA DATA READY
     * -------------------------------------------------------- */

    uint8_t status = 0;

    int tentativas = 0;


    while (tentativas < 300)
    {
        if (!vl53_read_reg8(
                RESULT_INTERRUPT_STATUS,
                &status))
        {
            return false;
        }


        if ((status & 0x07) != 0)
            break;


        vTaskDelay(
            pdMS_TO_TICKS(5)
        );


        tentativas++;
    }


    if (tentativas >= 300)
    {
        ESP_LOGE(
            TAG,
            "Timeout aguardando medicao"
        );

        return false;
    }


    /* --------------------------------------------------------
     * RANGE STATUS BRUTO
     * -------------------------------------------------------- */

    uint8_t range_status_raw = 0;


    if (!vl53_read_reg8(
            RESULT_RANGE_STATUS,
            &range_status_raw))
    {
        return false;
    }


    uint8_t range_status =
        (range_status_raw >> 3) & 0x1F;


    /* --------------------------------------------------------
     * DISTANCIA
     * -------------------------------------------------------- */

    uint16_t distance = 0;


    if (!vl53_read_reg16(
            RESULT_FINAL_RANGE_MM_HI,
            &distance))
    {
        return false;
    }


    /* --------------------------------------------------------
     * LOG
     * -------------------------------------------------------- */

    ESP_LOGI(
        TAG,
        "Status bruto: 0x%02X | Range status: %u | Raw distance: %u mm",
        range_status_raw,
        range_status,
        distance
    );


    /* --------------------------------------------------------
     * CLEAR INTERRUPT
     * -------------------------------------------------------- */

    if (!vl53_write_reg8(
            SYSTEM_INTERRUPT_CLEAR,
            0x01))
    {
        return false;
    }


    *distance_mm = distance;


    if (distance >= 2000)
    {
        ESP_LOGI(
            TAG,
            "Distancia: FORA DO ALCANCE (> 2.00 m)"
        );
    }
    else
    {
        ESP_LOGI(
            TAG,
            "Distancia: %.2f m",
            distance / 1000.0f
        );
    }


    return true;
}