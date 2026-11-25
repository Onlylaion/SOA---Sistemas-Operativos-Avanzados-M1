#ifndef PUERTA_ACTUATORS_H
#define PUERTA_ACTUATORS_H

#include "Definition.h"

void clearPasskeypad()
{
  memset(passKeypad, 0, sizeof(passKeypad));
  idxPassKeypad = 0;
}

void mostrar_pantalla_reposo()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ingrese Clave:");
}

void detener_timer_teclado()
{
  xTimerStop(timerInactividadTeclado, 0);
  // Vacía el semáforo por si quedó un timeout pendiente sin consumir
  xSemaphoreTake(semTimeoutTeclado, 0);
}

void motor_abrir()
{
  analogWrite(IN2, 0);
  analogWrite(IN1, VELOCIDAD_MOTOR);
}

void motor_cerrar()
{
  analogWrite(IN1, 0);
  analogWrite(IN2, VELOCIDAD_MOTOR);
}

void motor_detener()
{
  analogWrite(IN1, 0);
  analogWrite(IN2, 0);   // ambos en 0 = el motor queda en punto muerto
}

void tocar_melodia(const nota_t* melodia, size_t cantidad)
{
  for (size_t i = 0; i < cantidad; i++)
  {
    if (melodia[i].frecuencia > 0) 
      tone(BUZZER_PIN, melodia[i].frecuencia);
    else
      noTone(BUZZER_PIN);
    vTaskDelay(pdMS_TO_TICKS(melodia[i].duracion));
    noTone(BUZZER_PIN);
    vTaskDelay(pdMS_TO_TICKS(PAUSA_ENTRE_NOTAS));
  }
}

void v_task_buzzer(void *pvParameters)
{
  melodias pedido;
  while (1)
  {
    if (xQueueReceive(queueBuzzer, &pedido, portMAX_DELAY) == pdTRUE)
    {
      switch (pedido)
      {
        case MELODIA_ACCESO: 
          tocar_melodia(MELODIA_MARIO, CANT_NOTAS_MARIO);         
          break;
        case PITIDO_CUENTA:  
          tocar_melodia(PITIDO_CUENTA_REGRESIVA, CANT_NOTAS_PITIDO_CUENTA);
          break;
        case PITIDO_ERROR:   
          tocar_melodia(PITIDO_DNI_INVALIDO, CANT_NOTAS_PITIDO_ERROR);
          break;
      }
    }
  }
}

#endif // PUERTA_ACTUATORS_H
