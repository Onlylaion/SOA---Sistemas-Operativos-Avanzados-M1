#include "Definition.h"
#include "MQTT_conecction.h"
#include "REST_comunicacion.h"
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
  queueOcupacion = xQueueCreate(SIZE_QUEUE_OCUPACION, sizeof(int));
  queueREST = xQueueCreate(SIZE_QUEUE_REST, sizeof(char[TAM_DNI + 1]));

  mutexTimersCompartidos = xSemaphoreCreateMutex();
  semTimeoutTeclado = xSemaphoreCreateBinary();

  conectarWiFi();
  iniciarMQTT();

  xTaskCreate(v_loop_task,          "v_loop_task",          4096, NULL, 1, &loopNewEventHandler);
  xTaskCreate(v_task_get_new_event, "v_task_get_new_event", 4096, NULL, 1, &loopTaskHandler);
  xTaskCreate(v_task_mqtt,          "v_task_mqtt",          4096, NULL, 1, &mqttTaskHandler);
  xTaskCreate(v_task_client_http,   "v_task_client_http",   4096, NULL, 1, &restTaskHandler);
}
