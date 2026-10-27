#ifndef PUERTA_MQTT_CONECCTION_H
#define PUERTA_MQTT_CONECCTION_H

#include "Definition.h"

void conectarWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000)
  {
    delay(300);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("WiFi conectado");
  }
  else
  {
    Serial.println("WiFi: no se pudo conectar (el sistema sigue sin MQTT)");
  }
}

bool publicarMovimiento(int delta)            // delta = +1 al entrar, -1 al salir
{
  const char* topic = (delta > 0) ? TOPIC_APERTURA : TOPIC_SALIDA;
  char payload[24];
  snprintf(payload, sizeof(payload), "{\"cantidad\": %d}", delta);

  // QoS 1 sin retain: cada mensaje es un evento, el servidor lleva la cuenta
  if (esp_mqtt_client_publish(mqttClient, topic, payload, 0, 1, 0) >= 0)
  {
    Serial.println(String("MQTT: publicado ") + topic + " " + payload);
    return true;
  }
  Serial.println(String("MQTT: fallo el publish en ") + topic);
  return false;
}

// Corre en la tarea interna de esp-mqtt, no en v_task_mqtt
void mqttEventHandler(void *args, esp_event_base_t base, int32_t event_id, void *event_data)
{
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

  switch ((esp_mqtt_event_id_t)event_id)
  {
    case MQTT_EVENT_CONNECTED:
      Serial.println("MQTT conectado");
      mqttConectado = true;

      esp_mqtt_client_subscribe(mqttClient, TOPIC_COMANDO, 1);
      Serial.println(String("MQTT: suscripto a ") + TOPIC_COMANDO);
      break;

    case MQTT_EVENT_DISCONNECTED:
      Serial.println("MQTT desconectado, reintentando...");
      mqttConectado = false;
      break;
    
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("Tópico: %.*s\r\n", event->topic_len, event->topic);
        if (strncmp(event->topic, TOPIC_COMANDO, event->topic_len) == 0)
        {
          events newEvent = EV_CONTRASENIA_VALIDA;
          if (xQueueSend(queueEvents, &newEvent, pdMS_TO_TICKS(200)) != pdPASS)
          {
            Serial.println("MQTT: Queue is full, no se puede enviar el evento de abrir puerta.");
          }
        }
        break;

    case MQTT_EVENT_ERROR:
      if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
      {
        Serial.printf("MQTT: error de transporte, esp_err=0x%x tls_stack=0x%x errno=%d\n",
                      event->error_handle->esp_tls_last_esp_err,
                      event->error_handle->esp_tls_stack_err,
                      event->error_handle->esp_transport_sock_errno);
      }
      else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
      {
        // 4 = usuario/clave incorrectos, 5 = no autorizado
        Serial.printf("MQTT: conexion rechazada por el broker, codigo=%d\n",
                      event->error_handle->connect_return_code);
      }
      break;

    default:
      break;
  }
}

void iniciarMQTT()
{
  esp_mqtt_client_config_t cfg = {};
  cfg.broker.address.uri = MQTT_URI;
  cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.credentials.client_id = MQTT_CLIENT_ID;
  if (strlen(MQTT_USER) > 0)
  {
    cfg.credentials.username = MQTT_USER;
    cfg.credentials.authentication.password = MQTT_PASS;
  }
  cfg.session.keepalive = 30;
  cfg.network.reconnect_timeout_ms = INTERVALO_RECONEXION_MQTT;
  cfg.network.timeout_ms = 10000;   // el handshake TLS + WebSocket a través de Cloudflare puede tardar

  mqttClient = esp_mqtt_client_init(&cfg);
  esp_mqtt_client_register_event(mqttClient, MQTT_EVENT_ANY, mqttEventHandler, NULL);
  esp_mqtt_client_start(mqttClient);   // se conecta y reconecta solo, en su propia tarea
}

void v_task_mqtt(void *pvParameters)
{
  int delta;

  while (1)
  {
    // Sin conexión no se saca nada de la cola: los movimientos quedan esperando
    if (!mqttConectado)
    {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // Se mira sin sacar, y solo se descarta si el publish salió bien
    if (xQueuePeek(queueOcupacion, &delta, pdMS_TO_TICKS(100)) == pdTRUE)
    {
      if (publicarMovimiento(delta))
      {
        xQueueReceive(queueOcupacion, &delta, 0);
      }
      else
      {
        vTaskDelay(pdMS_TO_TICKS(500));
      }
    }
  }
}

void actualizarYPublicarOcupacion(int delta)                  // delta = +1 al entrar, -1 al salir
{
  if (xQueueSend(queueOcupacion, &delta, 0) != pdPASS)
  {
    Serial.println("MQTT: cola de movimientos llena, se pierde el evento");
  }
}

#endif // PUERTA_MQTT_CONECCTION_H
