#ifndef PUERTA_FSM_TRANSITION_H
#define PUERTA_FSM_TRANSITION_H

#include "Definition.h"
#include "MQTT_conecction.h"
#include "Actuators.h"

void init_sist()
{
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setTimeOut(50);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(GREEN_PULSADOR, INPUT_PULLUP);
  pinMode(RED_PULSADOR, INPUT_PULLUP);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  motor_detener();

  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, LOW);

  lcd.init();
  lcd.backlight();

  clearPasskeypad();
  mostrar_pantalla_reposo();

  timer_puerta_activo = false;
  mostrando_error = false;

  timerInactividadTeclado = xTimerCreate(
    "TimeoutTeclado",
    pdMS_TO_TICKS(TIMEOUT_INGRESO),
    pdFALSE,                       // one-shot
    NULL,
    callbackTimeoutTeclado
  );

  current_state = ST_PUERTA_CERRADA;
}

void none()  { }
void error() { }

void contrasenia_invalida()
{
  detener_timer_teclado();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DNI Incorrecto");
  digitalWrite(RED_LED, HIGH);     
  tone(BUZZER_PIN, 500, 400);      

  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  mostrando_error = true;
  error_timer = millis();
  xSemaphoreGive(mutexTimersCompartidos);

  clearPasskeypad();
}

void limpiar_error()               // también limpia el mensaje "Puerta Cerrada"
{
  digitalWrite(RED_LED, LOW);      
  
  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  mostrando_error = false;
  xSemaphoreGive(mutexTimersCompartidos);

  mostrar_pantalla_reposo();
}

void ingreso_tecla()
{
  bool ocupadoPorError;
  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  ocupadoPorError = mostrando_error;
  xSemaphoreGive(mutexTimersCompartidos);

  if (ocupadoPorError) return;     // ignora teclas mientras hay un mensaje en pantalla

  if (idxPassKeypad < TAM_DNI)
  {
    xSemaphoreTake(mutexUltimaTecla, portMAX_DELAY);
    passKeypad[idxPassKeypad] = ultimo_caracter;
    idxPassKeypad++;
    passKeypad[idxPassKeypad] = '\0';
    xSemaphoreGive(mutexUltimaTecla);

    lcd.setCursor(0, 1);
    lcd.print(passKeypad);

    xTimerReset(timerInactividadTeclado, 0);   // arranca o reinicia el conteo
  }
}

void resetear_teclado()
{
  detener_timer_teclado();
  clearPasskeypad();
  mostrar_pantalla_reposo();
}

void obtener_datos_teclado()
{
  MensajeQueue_t msg;
  msg.tipo = TIPO_CHAR;
  msg.valor_touch = 0;

  xSemaphoreTake(mutexUltimaTecla, portMAX_DELAY);
  strcpy(msg.passKeypad_mqtt, passKeypad);
  if (idxPassKeypad < TAM_DNI) 
  {
    msg.passKeypad_mqtt[idxPassKeypad + 1] = '\0';  // asegurar terminación nula
  } 
  xSemaphoreGive(mutexUltimaTecla);

  xQueueSend(queueDatos, &msg, 0);
}

void obtener_valor_touch()
{
  MensajeQueue_t msg;
  msg.tipo = TIPO_ENTERO;
  msg.valor_touch = touchRead(TOUCH_PIN);
  xQueueSend(queueDatos, &msg, 0);
}

void abrir_puerta()
{
  detener_timer_teclado();

  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  mostrando_error = false;
  xSemaphoreGive(mutexTimersCompartidos);

  digitalWrite(RED_LED, LOW);      // LED ROJO: apagado si venía de un error
  digitalWrite(GREEN_LED, HIGH);   // LED VERDE: abriendo
  lcd.clear();
  lcd.print("Abriendo...");
  motor_abrir();
  current_state = ST_ABRIENDO_PUERTA;
}

void abrir_puerta_ingreso()
{
  abrir_puerta();
  //actualizarYPublicarOcupacion(+1);
}

void abrir_puerta_salida()
{
  abrir_puerta();
  //actualizarYPublicarOcupacion(-1);
}

void fin_carrera_abierto()
{
  motor_detener();                
  digitalWrite(GREEN_LED, LOW);
  clearPasskeypad();

  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  lct = millis();
  timer_puerta_activo = true;
  xSemaphoreGive(mutexTimersCompartidos);

  tiempoRestante = TIEMPO_PUERTA_ABIERTA / 1000;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Tiempo restante:");
  lcd.setCursor(0, 1);
  lcd.print(tiempoRestante);

  current_state = ST_PUERTA_ABIERTA;
}

void mostrar_timer()
{
  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  unsigned long inicio = lct;
  xSemaphoreGive(mutexTimersCompartidos);

  int nuevoTiempo = (TIEMPO_PUERTA_ABIERTA / 1000) - (int)((millis() - inicio) / 1000);

  if (nuevoTiempo != tiempoRestante && nuevoTiempo >= 0)
  {
    tiempoRestante = nuevoTiempo;
    lcd.setCursor(0, 1);
    lcd.print(tiempoRestante);
    lcd.print("   ");

    if (tiempoRestante <= 5 && tiempoRestante > 0)
    {
      tone(BUZZER_PIN, 1500, 50);  
    }
  }
}

void cerrar_puerta()
{
  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  timer_puerta_activo = false;
  xSemaphoreGive(mutexTimersCompartidos);

  lcd.clear();
  lcd.print("Cerrando...");
  motor_cerrar();                  
  current_state = ST_CERRANDO_PUERTA;
}

void fin_carrera_cerrado()
{
  motor_detener();                 
  clearPasskeypad();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Puerta Cerrada");

  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  mostrando_error = true;
  error_timer = millis();
  xSemaphoreGive(mutexTimersCompartidos);

  current_state = ST_PUERTA_CERRADA;
}

#endif // PUERTA_FSM_TRANSITION_H
