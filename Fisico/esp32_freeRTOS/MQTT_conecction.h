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

void publicarOcupacion(int valor)
{
  char payload[8];
  sprintf(payload, "%d", valor);

  if (mqttClient.publish(TOPIC_OCUPACION, payload, true))
  {
    Serial.println(String("MQTT: publicado ") + payload);
  }
  else
  {
    Serial.println(String("MQTT: fallo el publish, state=") + mqttClient.state());
  }
}

void publicarEstado(char* estado)
{
  if (mqttClient.publish(TOPIC_APERTURA, estado, false))
  {
    Serial.println(String("MQTT: publicado ") + estado);
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
  if (strcmp(topic, TOPIC_COMANDO) != 0) return;


  // El payload NO viene terminado en '\0': hay que copiarlo
  char dni[TAM_DNI + 1];
  if (length == 0 || length > TAM_DNI) 
  {
    publicarEstado("ERROR");
    return;
  }
  memcpy(dni, payload, length);
  dni[length] = '\0';
  
  bool validacion = strcmp(dni, PASS_TEST) == 0;

  events ev = (validacion) ? EV_CONTRASENIA_VALIDA : EV_CONTRASENIA_INVALIDA;

  xQueueSend(queueEvents, &ev, 0);

  validacion ? publicarEstado("OK") : publicarEstado("ERROR");
}

void v_task_mqtt(void *pvParameters)
{
  int ocupacionActual = 0;
  unsigned long ultimoIntento = 0;
  bool primerIntento = true;

  while (1)
  {
    int nuevoValor;
    bool hayNuevoValor = (xQueueReceive(queueOcupacion, &nuevoValor, pdMS_TO_TICKS(50)) == pdTRUE);
    if (hayNuevoValor)
    {
      ocupacionActual = nuevoValor;
    }

    if (mqttClient.connected())
    {
      mqttClient.loop();
      if (hayNuevoValor)
      {
        publicarOcupacion(ocupacionActual);
      }
    }
    else if (primerIntento || millis() - ultimoIntento >= INTERVALO_RECONEXION_MQTT)
    {
      primerIntento = false;
      ultimoIntento = millis();
      if (conectarMQTT())
      {
        publicarOcupacion(ocupacionActual);
      }
    }
  }
}

void actualizarYPublicarOcupacion(int delta)                  // delta = +1 al entrar, -1 al salir
{
  cantidadPersonas += delta;

  if (cantidadPersonas < 0) 
  {
    cantidadPersonas = 0;
  }
  
  if (cantidadPersonas > CAPACIDAD_MAXIMA) 
  {
    cantidadPersonas = CAPACIDAD_MAXIMA;
  }

  xQueueOverwrite(queueOcupacion, &cantidadPersonas);         
}

#endif // PUERTA_MQTT_CONECCTION_H
