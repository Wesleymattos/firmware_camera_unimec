#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include <stdio.h>
#include <string.h>

#include "web_server.h"

#include <stdbool.h>
#include <stdint.h>

#include "modem.h"
#include "detector.h"
#include "vl53l0x.h"

static const char *TAG = "WEB_SERVER";

#define PART_BOUNDARY "123456789000000000000987654321"

static const char *STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;

static const char *STREAM_BOUNDARY =
    "\r\n--" PART_BOUNDARY "\r\n";

static const char *STREAM_PART =
    "Content-Type: image/jpeg\r\n"
    "Content-Length: %u\r\n\r\n";


static esp_err_t index_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>UNIMEC Camera</title>"
        "<style>"
        "body{background:#111;color:white;text-align:center;font-family:Arial}"
        "img{max-width:100%;height:auto}"
        "</style>"
        "</head>"
        "<body>"
        "<h1>UNIMEC Camera</h1>"
        "<img src='/stream'>"
        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html");

    return httpd_resp_send(
        req,
        html,
        HTTPD_RESP_USE_STRLEN
    );
}



static esp_err_t status_handler(httpd_req_t *req)
{
    uint16_t distancia_mm = 0;
    bool sensor_ok = vl53l0x_read_distance(&distancia_mm);
    bool pessoa_detectada = detector_pessoa_detectada();
    bool modem_ok = modem_is_connected();

    char json[256];

    snprintf(
        json,
        sizeof(json),
        "{"
        "\"modem_connected\":%s,"
        "\"sim_status\":\"N/D\","
        "\"signal\":\"N/D\","
        "\"sensor_ok\":%s,"
        "\"distance_mm\":%u,"
        "\"person_detected\":%s"
        "}",
        modem_ok ? "true" : "false",
        sensor_ok ? "true" : "false",
        sensor_ok ? (unsigned int)distancia_mm : 0,
        pessoa_detectada ? "true" : "false"
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}



static esp_err_t monitor_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html>"
        "<html lang='pt-BR'>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>UNIMEC | Monitor</title>"
        "<style>"
        "*{box-sizing:border-box}"
        "body{margin:0;background:#0b1120;color:#e5e7eb;"
        "font-family:Arial,sans-serif;padding:24px}"
        ".container{max-width:1000px;margin:auto}"
        "header{display:flex;justify-content:space-between;"
        "align-items:center;gap:12px;flex-wrap:wrap;margin-bottom:24px}"
        "h1{margin:0;font-size:25px}"
        ".sub{color:#94a3b8;margin-top:7px;font-size:13px}"
        ".pill{background:#172554;color:#93c5fd;padding:8px 12px;"
        "border-radius:20px;font-size:12px}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fit,"
        "minmax(220px,1fr));gap:16px}"
        ".card{background:#111c2f;border:1px solid #23314a;"
        "border-radius:16px;padding:20px;min-height:145px}"
        ".label{color:#94a3b8;font-size:13px}"
        ".value{font-size:27px;font-weight:bold;margin:14px 0 8px}"
        ".detail{font-size:12px;color:#94a3b8}"
        ".good{color:#34d399}.bad{color:#fb7185}.neutral{color:#fbbf24}"
        ".footer{margin-top:22px;color:#64748b;font-size:12px}"
        "a{color:#60a5fa;text-decoration:none}"
        "</style></head>"
        "<body><div class='container'>"
        "<header><div><h1>UNIMEC Monitor</h1>"
        "<div class='sub'>Monitoramento do dispositivo em tempo real</div>"
        "</div><div class='pill' id='updated'>Conectando...</div></header>"
        "<div class='grid'>"

        "<div class='card'><div class='label'>Conexão SIM7080G / PPP</div>"
        "<div class='value' id='modem'>--</div>"
        "<div class='detail'>Conexão de dados celular</div></div>"

        "<div class='card'><div class='label'>Estado do SIM</div>"
        "<div class='value neutral' id='sim'>N/D</div>"
        "<div class='detail'>Leitura do estado do chip</div></div>"

        "<div class='card'><div class='label'>Sinal celular</div>"
        "<div class='value neutral' id='signal'>N/D</div>"
        "<div class='detail'>RSSI / CSQ do modem</div></div>"

        "<div class='card'><div class='label'>Sensor VL53L0X</div>"
        "<div class='value' id='sensor'>--</div>"
        "<div class='detail'>Estado da leitura I2C</div></div>"

        "<div class='card'><div class='label'>Distância</div>"
        "<div class='value' id='distance'>--</div>"
        "<div class='detail'>Medição do sensor ToF</div></div>"

        "<div class='card'><div class='label'>Detecção de pessoa</div>"
        "<div class='value' id='person'>--</div>"
        "<div class='detail'>Estado do detector</div></div>"

        "</div><div class='footer'>"
        "Atualização automática a cada 2 segundos. "
        "<a href='/'>Abrir câmera</a>"
        "</div></div>"

        "<script>"
        "async function update(){"
        "try{"
        "const r=await fetch('/api/status',{cache:'no-store'});"
        "if(!r.ok)throw Error('HTTP '+r.status);"
        "const d=await r.json();"

        "const modem=document.getElementById('modem');"
        "modem.textContent=d.modem_connected?'CONECTADO':'DESCONECTADO';"
        "modem.className='value '+(d.modem_connected?'good':'bad');"

        "document.getElementById('sim').textContent=d.sim_status;"
        "document.getElementById('signal').textContent=d.signal;"

        "const sensor=document.getElementById('sensor');"
        "sensor.textContent=d.sensor_ok?'OPERACIONAL':'FALHA';"
        "sensor.className='value '+(d.sensor_ok?'good':'bad');"

        "document.getElementById('distance').textContent="
        "d.sensor_ok?(d.distance_mm/1000).toFixed(3)+' m':'--';"

        "const person=document.getElementById('person');"
        "person.textContent=d.person_detected?'DETECTADA':'NÃO DETECTADA';"
        "person.className='value '+(d.person_detected?'neutral':'good');"

        "document.getElementById('updated').textContent="
        "'Atualizado: '+new Date().toLocaleTimeString('pt-BR');"
        "}catch(e){"
        "document.getElementById('updated').textContent='Falha ao atualizar';"
        "console.error(e);"
        "}"
        "}"
        "update();setInterval(update,2000);"
        "</script></body></html>";

    httpd_resp_set_type(req, "text/html; charset=UTF-8");

    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    esp_err_t res = ESP_OK;

    httpd_resp_set_type(
        req,
        STREAM_CONTENT_TYPE
    );

    httpd_resp_set_hdr(
        req,
        "Access-Control-Allow-Origin",
        "*"
    );

    while (true)
    {
        camera_fb_t *fb = esp_camera_fb_get();

        if (fb == NULL)
        {
            ESP_LOGE(TAG, "Falha ao capturar frame");

            res = ESP_FAIL;
            break;
        }

        char part_buf[64];

        int hlen = snprintf(
            part_buf,
            sizeof(part_buf),
            STREAM_PART,
            fb->len
        );

        res = httpd_resp_send_chunk(
            req,
            STREAM_BOUNDARY,
            strlen(STREAM_BOUNDARY)
        );

        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(
                req,
                part_buf,
                hlen
            );
        }

        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(
                req,
                (const char *)fb->buf,
                fb->len
            );
        }

        esp_camera_fb_return(fb);

        if (res != ESP_OK)
        {
            break;
        }
    }

    return res;
}


void web_server_init(void)
{
    ESP_LOGI(TAG, "Iniciando servidor HTTP...");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = 80;

    httpd_handle_t server = NULL;

    esp_err_t err = httpd_start(
        &server,
        &config
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Falha ao iniciar servidor: %s",
            esp_err_to_name(err)
        );

        return;
    }

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL
    };

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL
    };

    httpd_uri_t monitor_uri = {
    .uri = "/monitor",
    .method = HTTP_GET,
    .handler = monitor_handler,
    .user_ctx = NULL
};

httpd_uri_t status_uri = {
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = status_handler,
    .user_ctx = NULL
};

httpd_register_uri_handler(server, &monitor_uri);
httpd_register_uri_handler(server, &status_uri);

    httpd_register_uri_handler(
        server,
        &index_uri
    );

    httpd_register_uri_handler(
        server,
        &stream_uri
    );

    ESP_LOGI(TAG, "Servidor HTTP iniciado!");
    ESP_LOGI(TAG, "Camera: http://192.168.100.4/");
    ESP_LOGI(TAG, "Stream: http://192.168.100.4/stream");
    ESP_LOGI(TAG, "Monitor: http://<IP-DO-ESP32>/monitor");
}

