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

/*
 * Stop variable lida durante a inicialização.
 * Ela precisa ser restaurada antes de cada single ranging.
 */
static uint8_t stop_variable = 0;


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
            "Erro escrevendo registrador 0x%02X: %s",
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
    {
        return false;
    }

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
            "Erro lendo registrador 0x%02X: %s",
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
            "Erro escrevendo registrador 16-bit 0x%02X: %s",
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

static bool vl53_read_reg16(uint8_t reg, uint16_t *value)
{
    if (value == NULL)
    {
        return false;
    }

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
            "Erro lendo registrador 16-bit 0x%02X: %s",
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
 * CALIBRAÇÃO DE REFERÊNCIA
 *
 * Esta é a sequência equivalente à:
 *
 * VL53L0X_perform_single_ref_calibration()
 *
 * usada por drivers baseados na API da ST.
 * ============================================================ */

static bool vl53_perform_single_ref_calibration(
    uint8_t vhv_init_byte)
{
    /*
     * Inicia a calibração.
     *
     * 0x01 = start
     * 0x40 = VHV init
     * 0x00 = phase calibration
     */

    if (!vl53_write_reg8(
            0x00,
            0x01 | vhv_init_byte))
    {
        return false;
    }


    /*
     * Aguarda o sensor indicar que terminou.
     *
     * RESULT_INTERRUPT_STATUS = 0x13
     */

    uint8_t status = 0;

    int tentativas = 0;

    while (tentativas < 200)
    {
        if (!vl53_read_reg8(
                0x13,
                &status))
        {
            return false;
        }


        /*
         * Os bits 0..2 indicam interrupção
         * de medição/calibração.
         */

        if ((status & 0x07) != 0)
        {
            break;
        }


        vTaskDelay(
            pdMS_TO_TICKS(5)
        );

        tentativas++;
    }


    if (tentativas >= 200)
    {
        ESP_LOGE(
            TAG,
            "Timeout na calibracao de referencia"
        );

        return false;
    }


    /*
     * Limpa interrupção.
     */

    if (!vl53_write_reg8(
            0x0B,
            0x01))
    {
        return false;
    }


    /*
     * Para a medição/calibração.
     */

    if (!vl53_write_reg8(
            0x00,
            0x00))
    {
        return false;
    }


    return true;
}


/* ============================================================
 * INICIALIZAÇÃO
 * ============================================================ */

bool vl53l0x_init(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Inicializando VL53L0X...");
    ESP_LOGI(TAG, "SDA: GPIO %d", VL53L0X_SDA_GPIO);
    ESP_LOGI(TAG, "SCL: GPIO %d", VL53L0X_SCL_GPIO);
    ESP_LOGI(
        TAG,
        "Endereco I2C: 0x%02X",
        VL53L0X_I2C_ADDRESS
    );


    /* ========================================================
     * CRIA BARRAMENTO I2C
     * ======================================================== */

    i2c_master_bus_config_t bus_config = {
        .i2c_port = VL53L0X_I2C_PORT,
        .sda_io_num = VL53L0X_SDA_GPIO,
        .scl_io_num = VL53L0X_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };


    esp_err_t err = i2c_new_master_bus(
        &bus_config,
        &i2c_bus
    );


    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Falha ao criar barramento I2C: %s",
            esp_err_to_name(err)
        );

        return false;
    }


    /* ========================================================
     * ADICIONA VL53L0X
     * ======================================================== */

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = VL53L0X_I2C_ADDRESS,
        .scl_speed_hz = 400000
    };


    err = i2c_master_bus_add_device(
        i2c_bus,
        &device_config,
        &vl53_device
    );


    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Falha ao adicionar VL53L0X: %s",
            esp_err_to_name(err)
        );

        return false;
    }


    /* ========================================================
     * TESTE DE COMUNICAÇÃO
     * ======================================================== */

    err = i2c_master_probe(
        i2c_bus,
        VL53L0X_I2C_ADDRESS,
        100
    );


    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "VL53L0X nao respondeu no endereco 0x%02X",
            VL53L0X_I2C_ADDRESS
        );

        return false;
    }


    ESP_LOGI(
        TAG,
        "VL53L0X respondeu no endereco 0x%02X",
        VL53L0X_I2C_ADDRESS
    );


    /* ========================================================
     * MODEL ID
     * ======================================================== */

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
            "Model ID inesperado!"
        );

        return false;
    }


    ESP_LOGI(
        TAG,
        "VL53L0X identificado corretamente!"
    );


    /* ========================================================
     * DATA INIT
     * ======================================================== */

    /*
     * Coloca I/O em modo 2V8.
     *
     * O bit 0 do registrador 0x89 habilita
     * EXTSUP_HV.
     */

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


    /*
     * Sequência DataInit.
     */

    if (!vl53_write_reg8(
            0x88,
            0x00))
    {
        return false;
    }


    if (!vl53_write_reg8(
            0x80,
            0x01))
    {
        return false;
    }


    if (!vl53_write_reg8(
            0xFF,
            0x01))
    {
        return false;
    }


    if (!vl53_write_reg8(
            0x00,
            0x00))
    {
        return false;
    }


    /*
     * Stop Variable.
     *
     * Este valor será usado em cada medição.
     */

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
    {
        return false;
    }


    if (!vl53_write_reg8(
            0xFF,
            0x00))
    {
        return false;
    }


    if (!vl53_write_reg8(
            0x80,
            0x00))
    {
        return false;
    }


    /* ========================================================
     * MSRC
     * ======================================================== */

    uint8_t msrc_control = 0;

    if (!vl53_read_reg8(
            0x60,
            &msrc_control))
    {
        return false;
    }


    if (!vl53_write_reg8(
            0x60,
            msrc_control | 0x12))
    {
        return false;
    }


    /* ========================================================
     * SIGNAL RATE LIMIT
     *
     * 0.25 MCPS
     *
     * Registrador 0x44 usa formato Q9.7.
     *
     * 0.25 * 128 = 32 = 0x0020
     * ======================================================== */

    if (!vl53_write_reg16(
            0x44,
            0x0020))
    {
        return false;
    }


    /* ========================================================
     * TUNING SETTINGS
     *
     * Sequência utilizada pelos drivers
     * baseados na API VL53L0X.
     * ======================================================== */

    if (!vl53_write_reg8(0xFF, 0x01))
        return false;

    if (!vl53_write_reg8(0x00, 0x00))
        return false;

    if (!vl53_write_reg8(0xFF, 0x00))
        return false;

    if (!vl53_write_reg8(0x09, 0x00))
        return false;

    if (!vl53_write_reg8(0x10, 0x00))
        return false;

    if (!vl53_write_reg8(0x11, 0x00))
        return false;

    if (!vl53_write_reg8(0x24, 0x01))
        return false;

    if (!vl53_write_reg8(0x25, 0xFF))
        return false;

    if (!vl53_write_reg8(0x75, 0x00))
        return false;

    if (!vl53_write_reg8(0xFF, 0x01))
        return false;

    if (!vl53_write_reg8(0x4E, 0x2C))
        return false;

    if (!vl53_write_reg8(0x48, 0x00))
        return false;

    if (!vl53_write_reg8(0x30, 0x20))
        return false;

    if (!vl53_write_reg8(0xFF, 0x00))
        return false;

    if (!vl53_write_reg8(0x30, 0x09))
        return false;

    if (!vl53_write_reg8(0x54, 0x00))
        return false;

    if (!vl53_write_reg8(0x31, 0x04))
        return false;

    if (!vl53_write_reg8(0x32, 0x03))
        return false;

    if (!vl53_write_reg8(0x40, 0x83))
        return false;

    if (!vl53_write_reg8(0x46, 0x25))
        return false;

    if (!vl53_write_reg8(0x60, 0x00))
        return false;

    if (!vl53_write_reg8(0x27, 0x00))
        return false;

    if (!vl53_write_reg8(0x50, 0x06))
        return false;

    if (!vl53_write_reg8(0x51, 0x00))
        return false;

    if (!vl53_write_reg8(0x52, 0x96))
        return false;

    if (!vl53_write_reg8(0x56, 0x08))
        return false;

    if (!vl53_write_reg8(0x57, 0x30))
        return false;

    if (!vl53_write_reg8(0x61, 0x00))
        return false;

    if (!vl53_write_reg8(0x62, 0x00))
        return false;

    if (!vl53_write_reg8(0x64, 0x00))
        return false;

    if (!vl53_write_reg8(0x65, 0x00))
        return false;

    if (!vl53_write_reg8(0x66, 0xA0))
        return false;

    if (!vl53_write_reg8(0xFF, 0x01))
        return false;

    if (!vl53_write_reg8(0x22, 0x32))
        return false;

    if (!vl53_write_reg8(0x47, 0x14))
        return false;

    if (!vl53_write_reg8(0x49, 0xFF))
        return false;

    if (!vl53_write_reg8(0x4A, 0x00))
        return false;

    if (!vl53_write_reg8(0xFF, 0x00))
        return false;

    if (!vl53_write_reg8(0x7A, 0x0A))
        return false;

    if (!vl53_write_reg8(0x7B, 0x00))
        return false;

    if (!vl53_write_reg8(0x78, 0x21))
        return false;

    if (!vl53_write_reg8(0xFF, 0x01))
        return false;

    if (!vl53_write_reg8(0x23, 0x34))
        return false;

    if (!vl53_write_reg8(0x42, 0x00))
        return false;

    if (!vl53_write_reg8(0x44, 0xFF))
        return false;

    if (!vl53_write_reg8(0x45, 0x26))
        return false;

    if (!vl53_write_reg8(0x46, 0x05))
        return false;

    if (!vl53_write_reg8(0x40, 0x40))
        return false;

    if (!vl53_write_reg8(0x0E, 0x06))
        return false;

    if (!vl53_write_reg8(0x20, 0x1A))
        return false;

    if (!vl53_write_reg8(0x43, 0x40))
        return false;

    if (!vl53_write_reg8(0xFF, 0x00))
        return false;

    if (!vl53_write_reg8(0x34, 0x03))
        return false;

    if (!vl53_write_reg8(0x35, 0x44))
        return false;

    if (!vl53_write_reg8(0xFF, 0x01))
        return false;

    if (!vl53_write_reg8(0x31, 0x04))
        return false;

    if (!vl53_write_reg8(0x4B, 0x09))
        return false;

    if (!vl53_write_reg8(0x4C, 0x05))
        return false;

    if (!vl53_write_reg8(0x4D, 0x04))
        return false;

    if (!vl53_write_reg8(0xFF, 0x00))
        return false;

    if (!vl53_write_reg8(0x44, 0x00))
        return false;

    if (!vl53_write_reg8(0x45, 0x20))
        return false;

    if (!vl53_write_reg8(0x47, 0x08))
        return false;

    if (!vl53_write_reg8(0x48, 0x28))
        return false;

    if (!vl53_write_reg8(0x67, 0x00))
        return false;

    if (!vl53_write_reg8(0x70, 0x04))
        return false;

    if (!vl53_write_reg8(0x71, 0x01))
        return false;

    if (!vl53_write_reg8(0x72, 0xFE))
        return false;

    if (!vl53_write_reg8(0x76, 0x00))
        return false;

    if (!vl53_write_reg8(0x77, 0x00))
        return false;

    if (!vl53_write_reg8(0xFF, 0x01))
        return false;

    if (!vl53_write_reg8(0x0D, 0x01))
        return false;

    if (!vl53_write_reg8(0xFF, 0x00))
        return false;

    if (!vl53_write_reg8(0x80, 0x01))
        return false;

    if (!vl53_write_reg8(0x01, 0xF8))
        return false;

    if (!vl53_write_reg8(0xFF, 0x01))
        return false;

    if (!vl53_write_reg8(0x8E, 0x01))
        return false;

    if (!vl53_write_reg8(0x00, 0x01))
        return false;

    if (!vl53_write_reg8(0xFF, 0x00))
        return false;

    if (!vl53_write_reg8(0x80, 0x00))
        return false;


    /* ========================================================
     * INTERRUPT CONFIG
     * ======================================================== */

    if (!vl53_write_reg8(
            0x0A,
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
            0x0B,
            0x01))
    {
        return false;
    }


    /* ========================================================
     * SEQUENCE CONFIG
     * ======================================================== */

    if (!vl53_write_reg8(
            0x01,
            0xE8))
    {
        return false;
    }


    /* ========================================================
     * CALIBRAÇÃO VHV
     * ======================================================== */

    ESP_LOGI(
        TAG,
        "Iniciando calibracao VHV..."
    );


    if (!vl53_write_reg8(
            0x01,
            0x01))
    {
        return false;
    }


    if (!vl53_perform_single_ref_calibration(
            0x40))
    {
        ESP_LOGE(
            TAG,
            "Falha na calibracao VHV"
        );

        return false;
    }


    ESP_LOGI(
        TAG,
        "Calibracao VHV concluida"
    );


    /* ========================================================
     * CALIBRAÇÃO DE FASE
     * ======================================================== */

    ESP_LOGI(
        TAG,
        "Iniciando calibracao de fase..."
    );


    if (!vl53_write_reg8(
            0x01,
            0x02))
    {
        return false;
    }


    if (!vl53_perform_single_ref_calibration(
            0x00))
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


    /* ========================================================
     * RESTAURA SEQUENCE CONFIG
     * ======================================================== */

    if (!vl53_write_reg8(
            0x01,
            0xE8))
    {
        return false;
    }


    ESP_LOGI(
        TAG,
        "VL53L0X inicializado e calibrado!"
    );

    ESP_LOGI(
        TAG,
        "========================================"
    );


    return true;
}


/* ============================================================
 * LEITURA DE DISTÂNCIA
 * ============================================================ */

bool vl53l0x_read_distance(uint16_t *distance_mm)
{
    if (distance_mm == NULL)
    {
        return false;
    }


    if (vl53_device == NULL)
    {
        ESP_LOGE(
            TAG,
            "Sensor nao inicializado!"
        );

        return false;
    }


    /* ========================================================
     * PREPARA SINGLE RANGING
     * ======================================================== */

    if (!vl53_write_reg8(
            0x80,
            0x01))
    {
        return false;
    }


    if (!vl53_write_reg8(
            0xFF,
            0x01))
    {
        return false;
    }


    if (!vl53_write_reg8(
            0x00,
            0x00))
    {
        return false;
    }


    /*
     * Restaura Stop Variable.
     */

    if (!vl53_write_reg8(
            0x91,
            stop_variable))
    {
        return false;
    }


    if (!vl53_write_reg8(
            0x00,
            0x01))
    {
        return false;
    }


    if (!vl53_write_reg8(
            0xFF,
            0x00))
    {
        return false;
    }


    if (!vl53_write_reg8(
            0x80,
            0x00))
    {
        return false;
    }


    /* ========================================================
     * INICIA SINGLE RANGING
     * ======================================================== */

    if (!vl53_write_reg8(
            0x00,
            0x01))
    {
        return false;
    }


    /* ========================================================
     * AGUARDA RESULTADO
     * ======================================================== */

    uint8_t status = 0;

    int tentativas = 0;


    while (tentativas < 200)
    {
        /*
         * Primeiro verificamos se o start bit
         * foi limpo pelo sensor.
         */

        uint8_t start = 0;

        if (!vl53_read_reg8(
                0x00,
                &start))
        {
            return false;
        }


        /*
         * Depois verificamos o interrupt status.
         */

        if (!vl53_read_reg8(
                0x13,
                &status))
        {
            return false;
        }


        if ((status & 0x07) != 0)
        {
            break;
        }


        vTaskDelay(
            pdMS_TO_TICKS(5)
        );

        tentativas++;
    }


    if (tentativas >= 200)
    {
        ESP_LOGE(
            TAG,
            "Timeout aguardando medicao"
        );

        return false;
    }


    /* ========================================================
     * LE DISTÂNCIA
     * ======================================================== */

    uint16_t distance = 0;


    if (!vl53_read_reg16(
            0x1E,
            &distance))
    {
        return false;
    }


    /* ========================================================
     * LIMPA INTERRUPÇÃO
     * ======================================================== */

    if (!vl53_write_reg8(
            0x0B,
            0x01))
    {
        return false;
    }


    *distance_mm = distance;


    if (distance > 2000)
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