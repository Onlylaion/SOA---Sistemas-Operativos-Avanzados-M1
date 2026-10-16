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


void publicarTexto(const char* topic, const char* msg, bool retain = false)
{
  if (mqttClient.publish(topic, msg, retain))
    Serial.println(String("MQTT: ") + topic + " <- " + msg);
  else
    Serial.println(String("MQTT: fallo publish, state=") + mqttClient.state());
}

// Se llama desde cualquier tarea: solo encola, no toca mqttClient
void enviarNotificacion(notifs n)
{
  if (queueNotif) xQueueSend(queueNotif, &n, 0);
}

void callbackMQTT(char* topic, byte* payload, unsigned int length)
{
  if (strcmp(topic, TOPIC_COMANDO) != 0) return;

  if (length == 0 || length > TAM_DNI)
  {
    enviarNotificacion(NOTIF_APERTURA_ERROR);
    return;
  }

  char dni[TAM_DNI + 1];
  memcpy(dni, payload, length);
  dni[length] = '\0';

  events ev = (strcmp(dni, PASS_TEST) == 0) ? EV_CONTRASENIA_VALIDA : EV_CONTRASENIA_INVALIDA;
  xQueueSend(queueEvents, &ev, 0);
  // La respuesta OK/ERROR la manda la transición de la FSM (ver abajo)
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
    if (hayNuevoValor) ocupacionActual = nuevoValor;

    if (mqttClient.connected())
    {
      mqttClient.loop();

      if (hayNuevoValor)
      {
        char buf[8];
        sprintf(buf, "%d", ocupacionActual);
        publicarTexto(TOPIC_OCUPACION, buf, true);
      }

      notifs n;
      while (xQueueReceive(queueNotif, &n, 0) == pdTRUE)
      {
        switch (n)
        {
          case NOTIF_APERTURA_OK:    publicarTexto(TOPIC_APERTURA, "OK");      break;
          case NOTIF_APERTURA_ERROR: publicarTexto(TOPIC_APERTURA, "ERROR");   break;
          case NOTIF_SALIDA:         publicarTexto(TOPIC_SALIDA,   "SALIDA");  break;
        }
      }
    }
    else
    {
      notifs descarte;
      while (xQueueReceive(queueNotif, &descarte, 0) == pdTRUE) { }  // sin conexión se descartan

      if (primerIntento || millis() - ultimoIntento >= INTERVALO_RECONEXION_MQTT)
      {
        primerIntento = false;
        ultimoIntento = millis();
        if (conectarMQTT())
        {
          char buf[8];
          sprintf(buf, "%d", ocupacionActual);
          publicarTexto(TOPIC_OCUPACION, buf, true);
        }
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
