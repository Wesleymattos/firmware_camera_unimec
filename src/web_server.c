#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include <stdio.h>
#include <string.h>

#include "web_server.h"

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
        "img{max-width:100%%;height:auto}"
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
}