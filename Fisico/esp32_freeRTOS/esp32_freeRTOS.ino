#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <string.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ============================================================
// Definition.h
// ============================================================
#define SERIAL_DEBUG_ENABLED 1
#if SERIAL_DEBUG_ENABLED
  #define DebugPrint(str) { Serial.println(str); }
#else
  #define DebugPrint(str)
#endif

#define DebugPrintEstado(estado, evento) { \
  String est = estado; String evt = evento; String str; \
  str = "EST-> [" + est + "]: EVT-> [" + evt + "]."; \
  DebugPrint(str); \
}

#define TAM_DNI 8
#define PASS_TEST "1234567"
#define TIEMPO_PUERTA_ABIERTA 10000   // ms
#define TIEMPO_ERROR 3000             // ms que se muestra un mensaje temporal
#define TIMEOUT_INGRESO 5000          // ms de inactividad del teclado

// ---- WiFi ----
const char* WIFI_SSID = "TeleCentro-85b5";
const char* WIFI_PASS = "TKHZNZUMNJRZ";

// ---- MQTT ----
const char* MQTT_BROKER    = "192.168.0.7";
const uint16_t MQTT_PORT   = 1883;
const char* MQTT_CLIENT_ID = "esp32-puerta-gym";
#define TOPIC_COMANDO "gimnasio/puerta/comando"         // Topic donde se recibe el comando
#define TOPIC_APERTURA "gimnasio/puerta/apertura"       // Topic donde se publica el estado de la puerta
#define TOPIC_OCUPACION "gimnasio/ocupacion/cantidad"   // Topic donde se publica la cantidad de gente
#define CAPACIDAD_MAXIMA 50                             // Tope físico del gimnasio
int cantidadPersonas = 0;                               // Contador de ocupación actual, arranca en 0

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ---- Buzzer ----
const int BUZZER_PIN = 13;

// ---- Finales de carrera ----
const int GREEN_PULSADOR = 25;   // fin de carrera: puerta abierta
const int RED_PULSADOR   = 26;   // fin de carrera: puerta cerrada

// ---- LEDs ----
const int RED_LED   = 32;
const int GREEN_LED = 33;

// ---- Display I2C ----
const int SCL_PIN = 22;
const int SDA_PIN = 21;
const byte ADDR_DISPLAY = 0x27;
LiquidCrystal_I2C lcd(ADDR_DISPLAY, 16, 2);

// ---- Keypad ----
const uint8_t ROWS = 4;
const uint8_t COLS = 3;
char keys[ROWS][COLS] = {
  { '1', '2', '3' },
  { '4', '5', '6' },
  { '7', '8', '9' },
  { '*', '0', '#' }
};
uint8_t colPins[COLS] = { 16, 4, 27 };
uint8_t rowPins[ROWS] = { 23, 19, 18, 17 };
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

char passKeypad[TAM_DNI + 1];
int  idxPassKeypad = 0;
char ultimo_caracter = '\0';

// ---- Puente H DRV8833 ----
#define IN1 12
#define IN2 14
#define VELOCIDAD_MOTOR 127

// ---- Sensor touch capacitivo ----
#define TOUCH_PIN 15
#define UMBRAL_TOUCH 1000        
bool touch_mantenido = false;    // evita disparar el evento en cada lectura mientras siga tocado

// ---- Timeout de teclado - xTimer y semáforo binario ----
TimerHandle_t     timerInactividadTeclado = NULL;
SemaphoreHandle_t semTimeoutTeclado = NULL;

// ---- Timeout de puerta - millis() ----
bool timer_puerta_activo = false;
unsigned long lct = 0;
int tiempoRestante = TIEMPO_PUERTA_ABIERTA / 1000;

// ---- Mensaje temporal en pantalla (error / puerta cerrada) ----
bool mostrando_error = false;
unsigned long error_timer = 0;

// Mutex que protege lct/timer_puerta_activo/error_timer/mostrando_error,
// compartido entre v_loop_task (escribe) y v_task_get_new_event (lee).
SemaphoreHandle_t mutexTimersCompartidos = NULL;

enum states { ST_ARRANQUE, ST_PUERTA_CERRADA, ST_ABRIENDO_PUERTA, ST_PUERTA_ABIERTA, ST_CERRANDO_PUERTA } current_state = ST_ARRANQUE;
String states_s[] = {"ST_ARRANQUE", "ST_PUERTA_CERRADA", "ST_ABRIENDO_PUERTA", "ST_PUERTA_ABIERTA", "ST_CERRANDO_PUERTA"};

enum events {
  EV_CONT,
  EV_CONTRASENIA_INVALIDA,
  EV_CONTRASENIA_VALIDA,
  EV_TOUCH_DETECTADO,
  EV_FIN_CARRERA_ABIERTO,
  EV_FIN_CARRERA_CERRADO,
  EV_INGRESO_TECLA,
  EV_TIMEOUT_PUERTA,
  EV_TIMEOUT_ERROR,
  EV_TIMEOUT_TECLADO,
  EV_CANCELAR_TECLADO,
  EV_UNKNOW
};
String events_s[] = {
  "EV_CONT", "EV_CONTRASENIA_INVALIDA", "EV_CONTRASENIA_VALIDA", "EV_TOUCH_DETECTADO",
  "EV_FIN_CARRERA_ABIERTO", "EV_FIN_CARRERA_CERRADO", "EV_INGRESO_TECLA", "EV_TIMEOUT_PUERTA",
  "EV_TIMEOUT_ERROR", "EV_TIMEOUT_TECLADO", "EV_CANCELAR_TECLADO", "EV_UNKNOW"
};

#define MAX_STATES 5
#define MAX_EVENTS 11
#define MAX_TIPO_EVENTOS 6

typedef void (*transition)();

// Transiciones
void init_sist();
void none();
void error();
void contrasenia_invalida();
void limpiar_error();
void abrir_puerta();
void abrir_puerta_ingreso();
void abrir_puerta_salida();
void cerrar_puerta();
void ingreso_tecla();
void resetear_teclado();
void fin_carrera_abierto();
void fin_carrera_cerrado();
void mostrar_timer();
void callbackTimeoutTeclado(TimerHandle_t xTimer);

// Verificadores de eventos
events verificarTimeoutKeypad();
events verificarTimeoutPuerta();
events verificarTimeoutError();
events verificarEstadoSensorKeypad();
events verificarFinCarrera();
events verificarEstadoSensorTouch();

events (*verificar_sensor[MAX_TIPO_EVENTOS])() = {
  verificarTimeoutKeypad, verificarTimeoutPuerta, verificarTimeoutError,
  verificarEstadoSensorTouch, verificarEstadoSensorKeypad, verificarFinCarrera
};

// ------------------------------------------------------------
// Matriz de estados
// ------------------------------------------------------------
transition state_table[MAX_STATES][MAX_EVENTS] =
{
  //EV_CONT      , EV_CONTRASENIA_INVALIDA, EV_CONTRASENIA_VALIDA , EV_TOUCH_DETECTADO , EV_FIN_CARRERA_ABIERTO, EV_FIN_CARRERA_CERRADO, EV_INGRESO_TECLA, EV_TIMEOUT_PUERTA, EV_TIMEOUT_ERROR, EV_TIMEOUT_TECLADO, EV_CANCELAR_TECLADO
  { init_sist    , error                  , error                 , error              , error                 , error                 , error          , error            , error          , error             , error            }, // ST_ARRANQUE
  { none         , contrasenia_invalida   , abrir_puerta_ingreso  , abrir_puerta_salida, error                 , error                 , ingreso_tecla  , error            , limpiar_error  , resetear_teclado  , resetear_teclado }, // ST_PUERTA_CERRADA
  { none         , error                  , error                 , error              , fin_carrera_abierto   , error                 , error          , error            , error          , error             , error            }, // ST_ABRIENDO_PUERTA
  { mostrar_timer, error                  , error                 , cerrar_puerta      , error                 , error                 , error          , cerrar_puerta    , error          , error             , error            }, // ST_PUERTA_ABIERTA
  { none         , error                  , error                 , error              , error                 , fin_carrera_cerrado   , error          , error            , error          , error             , error            }  // ST_CERRANDO_PUERTA
};

#define SIZE_QUEUE_TIMER 30
TaskHandle_t loopTaskHandler = NULL;
TaskHandle_t loopNewEventHandler = NULL;
QueueHandle_t queueEvents = NULL;

#define INTERVALO_RECONEXION_MQTT 5000   
QueueHandle_t queueOcupacion = NULL;     
TaskHandle_t  mqttTaskHandler = NULL;

// ============================================================
// MQTT_conecction.h
// ============================================================
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

// ============================================================
// Sensors.h
// ============================================================
events verificarEstadoSensorKeypad()
{
  events new_event = EV_CONT;
  char tecla = keypad.getKey();

  if (tecla != NO_KEY)
  {
    if (tecla == '#')
    {
      new_event = (strcmp(passKeypad, PASS_TEST) == 0)
                    ? EV_CONTRASENIA_VALIDA : EV_CONTRASENIA_INVALIDA;
    }
    else if (tecla == '*')
    {
      new_event = EV_CANCELAR_TECLADO;
    }
    else
    {
      ultimo_caracter = tecla;
      new_event = EV_INGRESO_TECLA;
    }
  }
  return new_event;
}

events verificarFinCarrera()
{
  events new_event = EV_CONT;
  static int previo_green = HIGH;
  static int previo_red   = HIGH;

  int actual_green = digitalRead(GREEN_PULSADOR);
  int actual_red   = digitalRead(RED_PULSADOR);

  if (actual_green != previo_green)
  {
    previo_green = actual_green;
    if (actual_green == LOW) new_event = EV_FIN_CARRERA_ABIERTO;
  }

  if (actual_red != previo_red)
  {
    previo_red = actual_red;
    if (actual_red == LOW) new_event = EV_FIN_CARRERA_CERRADO;
  }
  return new_event;
}

events verificarEstadoSensorTouch()
{
  events new_event = EV_CONT;
  bool tocado = (touchRead(TOUCH_PIN) < UMBRAL_TOUCH);

  if (tocado && !touch_mantenido)
  {
    touch_mantenido = true;          // recién empieza el toque
    new_event = EV_TOUCH_DETECTADO;
  }
  else if (!tocado && touch_mantenido)
  {
    touch_mantenido = false;         // se rearma para el próximo toque
  }
  return new_event;
}

// ============================================================
// Timers.h
// ============================================================

void callbackTimeoutTeclado(TimerHandle_t xTimer)
{
  xSemaphoreGive(semTimeoutTeclado);
}

events verificarTimeoutKeypad()
{
  events new_event = EV_CONT;
  // Toma no bloqueante (timeout 0)
  if (xSemaphoreTake(semTimeoutTeclado, 0) == pdTRUE)
  {
    new_event = EV_TIMEOUT_TECLADO;
  }
  return new_event;
}

events verificarTimeoutPuerta()
{
  events new_event = EV_CONT;

  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  bool activo = timer_puerta_activo;
  unsigned long inicio = lct;
  xSemaphoreGive(mutexTimersCompartidos);

  if (activo && (millis() - inicio >= TIEMPO_PUERTA_ABIERTA))
  {
    new_event = EV_TIMEOUT_PUERTA;
  }
  return new_event;
}

events verificarTimeoutError()
{
  events new_event = EV_CONT;

  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  bool hayError = mostrando_error;
  unsigned long inicio = error_timer;
  xSemaphoreGive(mutexTimersCompartidos);

  if (hayError && (millis() - inicio >= TIEMPO_ERROR))
  {
    new_event = EV_TIMEOUT_ERROR;
  }
  return new_event;
}

// ============================================================
// Actuators.h (display / buffer del teclado / timer del teclado / motor)
// ============================================================
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

// ============================================================
// FSM_Transition.h
// ============================================================
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
    passKeypad[idxPassKeypad] = ultimo_caracter;
    idxPassKeypad++;
    passKeypad[idxPassKeypad] = '\0';
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
  actualizarYPublicarOcupacion(+1);
}

void abrir_puerta_salida()
{
  abrir_puerta();
  actualizarYPublicarOcupacion(-1);
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

// ============================================================
// esp32-blink.ino
// ============================================================
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
  queueOcupacion = xQueueCreate(1, sizeof(int));

  mutexTimersCompartidos = xSemaphoreCreateMutex();
  semTimeoutTeclado = xSemaphoreCreateBinary();

  conectarWiFi();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setSocketTimeout(2);      // segundos: evita bloqueos largos si el broker no responde
  mqttClient.setCallback(callbackMQTT);

  xTaskCreate(v_loop_task,          "v_loop_task",          4096, NULL, 1, &loopNewEventHandler);
  xTaskCreate(v_task_get_new_event, "v_task_get_new_event", 4096, NULL, 1, &loopTaskHandler);
  xTaskCreate(v_task_mqtt,          "v_task_mqtt",          4096, NULL, 1, &mqttTaskHandler);
}