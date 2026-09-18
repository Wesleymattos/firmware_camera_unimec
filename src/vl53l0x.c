#include "vl53l0x.h"

#include "esp_log.h"
#include "esp_err.h"

#include "driver/i2c_master.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


static const char *TAG = "VL53L0X";


/* ============================================================
 * HANDLE DO I2C
 * ============================================================ */

static i2c_master_bus_handle_t i2c_bus = NULL;

static i2c_master_dev_handle_t vl53l0x_device = NULL;


/* ============================================================
 * REGISTRADORES VL53L0X
 * ============================================================ */

#define VL53L0X_REG_IDENTIFICATION_MODEL_ID       0xC0
#define VL53L0X_REG_SYSTEM_SEQUENCE_CONFIG        0x01

#define VL53L0X_REG_SYSRANGE_START               0x00

#define VL53L0X_REG_RESULT_INTERRUPT_STATUS      0x13

#define VL53L0X_REG_RESULT_RANGE_STATUS           0x14

#define VL53L0X_REG_SYSTEM_INTERRUPT_CLEAR       0x0B


/* ============================================================
 * FUNCAO AUXILIAR - ESCREVER REGISTRADOR
 * ============================================================ */

static esp_err_t vl53l0x_write_register(
    uint8_t reg,
    uint8_t value
)
{
    uint8_t data[2];

    data[0] = reg;
    data[1] = value;

    return i2c_master_transmit(
        vl53l0x_device,
        data,
        sizeof(data),
        1000
    );
}


/* ============================================================
 * FUNCAO AUXILIAR - LER REGISTRADOR
 * ============================================================ */

static esp_err_t vl53l0x_read_register(
    uint8_t reg,
    uint8_t *value
)
{
    return i2c_master_transmit_receive(
        vl53l0x_device,
        &reg,
        1,
        value,
        1,
        1000
    );
}


/* ============================================================
 * INICIALIZACAO I2C
 * ============================================================ */

static bool vl53l0x_i2c_init(void)
{
    ESP_LOGI(
        TAG,
        "Inicializando barramento I2C..."
    );


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
            "Falha ao criar barramento I2C: %s",
            esp_err_to_name(err)
        );

        return false;
    }


    i2c_device_config_t device_config = {

        .dev_addr_length = I2C_ADDR_BIT_LEN_7,

        .device_address = VL53L0X_I2C_ADDRESS,

        .scl_speed_hz = 400000
    };


    err =
        i2c_master_bus_add_device(
            i2c_bus,
            &device_config,
            &vl53l0x_device
        );


    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Falha ao adicionar VL53L0X ao I2C: %s",
            esp_err_to_name(err)
        );

        return false;
    }


    ESP_LOGI(
        TAG,
        "I2C inicializado"
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


    return true;
}


/* ============================================================
 * INICIALIZACAO DO VL53L0X
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
        "========================================"
    );


    if (!vl53l0x_i2c_init())
    {
        return false;
    }


    vTaskDelay(
        pdMS_TO_TICKS(10)
    );


    /* --------------------------------------------------------
     * Verifica comunicacao com o sensor
     * -------------------------------------------------------- */

    uint8_t model_id = 0;

    esp_err_t err =
        vl53l0x_read_register(
            VL53L0X_REG_IDENTIFICATION_MODEL_ID,
            &model_id
        );


    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Nao foi possivel comunicar com o VL53L0X: %s",
            esp_err_to_name(err)
        );

        return false;
    }


    ESP_LOGI(
        TAG,
        "Model ID: 0x%02X",
        model_id
    );


    /* --------------------------------------------------------
     * Configuracao basica
     * -------------------------------------------------------- */

    err =
        vl53l0x_write_register(
            VL53L0X_REG_SYSTEM_SEQUENCE_CONFIG,
            0xFF
        );


    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Falha na configuracao inicial: %s",
            esp_err_to_name(err)
        );

        return false;
    }


    ESP_LOGI(
        TAG,
        "VL53L0X inicializado com sucesso!"
    );


    return true;
}


/* ============================================================
 * LEITURA DE DISTANCIA
 * ============================================================ */

bool vl53l0x_read_distance(
    uint16_t *distance_mm
)
{
    if (
        distance_mm == NULL ||
        vl53l0x_device == NULL
    )
    {
        return false;
    }


    /* --------------------------------------------------------
     * Inicia medicao
     * -------------------------------------------------------- */

    esp_err_t err =
        vl53l0x_write_register(
            VL53L0X_REG_SYSRANGE_START,
            0x01
        );


    if (err != ESP_OK)
    {
        return false;
    }


    /* --------------------------------------------------------
     * Aguarda medicao
     * -------------------------------------------------------- */

    for (int i = 0; i < 100; i++)
    {
        uint8_t status = 0;


        err =
            vl53l0x_read_register(
                VL53L0X_REG_RESULT_INTERRUPT_STATUS,
                &status
            );


        if (err != ESP_OK)
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
    }


    /* --------------------------------------------------------
     * Leitura do resultado
     * -------------------------------------------------------- */

    uint8_t range_data[2];


    uint8_t reg =
        VL53L0X_REG_RESULT_RANGE_STATUS;


    err =
        i2c_master_transmit_receive(
            vl53l0x_device,
            &reg,
            1,
            range_data,
            2,
            1000
        );


    if (err != ESP_OK)
    {
        return false;
    }


    uint16_t range_mm =
        ((uint16_t)range_data[0] << 8) |
        range_data[1];


    *distance_mm = range_mm;


    /* --------------------------------------------------------
     * Limpa interrupcao
     * -------------------------------------------------------- */

    vl53l0x_write_register(
        VL53L0X_REG_SYSTEM_INTERRUPT_CLEAR,
        0x01
    );


    return true;
}