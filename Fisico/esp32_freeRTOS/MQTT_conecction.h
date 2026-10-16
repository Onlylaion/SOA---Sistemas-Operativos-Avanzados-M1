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

void publicarSensorTouch(int valor)
{
  char payload[8];
  sprintf(payload, "%d", valor);

  if (mqttClient.publish(TOPIC_DATOS, payload, false))
  {
    Serial.println(String("MQTT: publicado ") + payload);
  }
  else
  {
    Serial.println(String("MQTT: fallo el publish, state=") + mqttClient.state());
  }
}

void publicarSensorKeypad(char* passKeypad)
{
  if (mqttClient.publish(TOPIC_DATOS, passKeypad, false))
  {
    Serial.println(String("MQTT: publicado ") + passKeypad);
  }
  else
  {
    Serial.println(String("MQTT: fallo el publish, state=") + mqttClient.state());
  }
}

bool conectarMQTT()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("MQTT: sin WiFi");
    return false;
  }

  if (mqttClient.connect(MQTT_CLIENT_ID))
  {
    Serial.println("MQTT conectado");
    mqttClient.subscribe(TOPIC_COMANDO);
    return true;
  }

  Serial.println(String("MQTT: fallo la conexion, state=") + mqttClient.state());
  return false;
}

void callbackMQTT(char* topic, byte* payload, unsigned int length)
{
  if (strcmp(topic, TOPIC_COMANDO) == 0)
  {
    Serial.println("MQTT: comando recibido");
    Serial.println("Topic: " + String(topic));
  }

  events ev = EV_COMANDO_MQTT;

  xQueueSend(queueEvents, &ev, 0);
}

void publicarMensaje(MensajeQueue_t &msg)
{
  if (msg.tipo == TIPO_ENTERO)
  {
    publicarSensorTouch(msg.valor_touch);
  }
  else if (msg.tipo == TIPO_CHAR)
  {
    publicarSensorKeypad(msg.passKeypad_mqtt);
  }
  else
  {
    Serial.println("MQTT: tipo de mensaje desconocido");
  }
}

void v_task_mqtt(void *pvParameters)
{
  MensajeQueue_t msg;
  unsigned long ultimoIntento = 0;
  bool primerIntento = true;

  while (1)
  {
    bool hayNuevoValor = (xQueueReceive(queueDatos, &msg, pdMS_TO_TICKS(50)) == pdTRUE);

    if (mqttClient.connected())
    {
      mqttClient.loop();
      if (hayNuevoValor)
      {
        publicarMensaje(msg);
      }
    }
    else if (primerIntento || millis() - ultimoIntento >= INTERVALO_RECONEXION_MQTT)
    {
      primerIntento = false;
      ultimoIntento = millis();
      
      if (conectarMQTT() && hayNuevoValor)
      {
        publicarMensaje(msg);
      }
    }
  }
}

#endif // PUERTA_MQTT_CONECCTION_H
