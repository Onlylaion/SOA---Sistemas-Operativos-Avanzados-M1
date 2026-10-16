#include "Definition.h"
#include "MQTT_conecction.h"
#include "Sensors.h"
#include "Timers.h"
#include "Actuators.h"
#include "FSM_Transition.h"

void v_task_get_new_event(void *pvParameters)
{
  enum events new_event;
  short indice = 0;

  while (1)
  {
    indice = (indice + 1) % MAX_TIPO_EVENTOS;

    new_event = verificar_sensor[indice]();

    if (xQueueSend(queueEvents, &new_event, pdMS_TO_TICKS(200)) != pdPASS)
    {
      Serial.println("Get_event TIMEOUT: Queue is full.");
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void maquina_de_estados()
{
  enum events new_event;

  if (xQueueReceive(queueEvents, &new_event, portMAX_DELAY) == pdTRUE)
  {
    if ((int)new_event < MAX_EVENTS && (int)current_state < MAX_STATES)
    {
      if (new_event != EV_CONT)
      {
        DebugPrintEstado(states_s[current_state], events_s[new_event]);
      }
      state_table[current_state][new_event]();
    }
  }
}

void v_loop_task(void *pvParameters)
{
  while (1)
  {
    maquina_de_estados();
  }
}

void loop() { }

void setup()
{
  Serial.begin(115200);
  queueEvents = xQueueCreate(SIZE_QUEUE_TIMER, sizeof(events));
  queueDatos = xQueueCreate(SIZE_QUEUE_DATOS, sizeof(MensajeQueue_t));

  mutexTimersCompartidos = xSemaphoreCreateMutex();
  mutexUltimaTecla = xSemaphoreCreateMutex();
  semTimeoutTeclado = xSemaphoreCreateBinary();

  conectarWiFi();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setSocketTimeout(2);      // segundos: evita bloqueos largos si el broker no responde
  mqttClient.setCallback(callbackMQTT);

  xTaskCreate(v_loop_task,          "v_loop_task",          4096, NULL, 1, &loopNewEventHandler);
  xTaskCreate(v_task_get_new_event, "v_task_get_new_event", 4096, NULL, 1, &loopTaskHandler);
  xTaskCreate(v_task_mqtt,          "v_task_mqtt",          4096, NULL, 1, &mqttTaskHandler);
}
