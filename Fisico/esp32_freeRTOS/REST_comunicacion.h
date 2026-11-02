#include "Definition.h"

int enviar_dni_REST(char* dni)
{
    int validado = -1;
    if (WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;
        String url = "https://soa.alfatechnologies.com.ar/gimnasio/puerta/apertura/";
        url += "?dni=" + String(dni);
        http.begin(url);
        http.addHeader("Content-Type", "application/json");
    
        int httpResponseCode = http.GET();
        if (httpResponseCode > 0) {
            switch (httpResponseCode) {
                case 200:
                    validado = 1;
                    Serial.println("REST: DNI válido, se puede abrir la puerta.");
                    break;
                case 401:
                    validado = 0;
                    Serial.println("REST: DNI inválido, no se puede abrir la puerta.");
                    break;
                default:
                    validado = -1;
                    Serial.println("REST: Error en la solicitud, código HTTP: " + String(httpResponseCode));
                    break;
            }
        } else {
            Serial.println("Error on sending GET: " + String(httpResponseCode));
        }
        http.end();
    }
    return validado;
}

void v_task_client_http(void *pvParameters)
{
    while (1)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("REST: WiFi no conectado, reintentando...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        char dni[TAM_DNI + 1];
        if (xQueueReceive(queueREST, &dni, pdMS_TO_TICKS(200)) == pdTRUE)
        {
            int resultado = enviar_dni_REST(dni);
            events newEvent = (resultado == 1) ? EV_CONTRASENIA_VALIDA : EV_CONTRASENIA_INVALIDA;
            if (xQueueSend(queueEvents, &newEvent, pdMS_TO_TICKS(200)) != pdPASS)
            {
                Serial.println("REST: Queue is full, no se puede enviar el evento de validación.");
            }
        }
    }
}