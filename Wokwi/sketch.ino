#include <Wire.h>
#include <LiquidCrystal_I2C.h>  //Necesario para poder comunicarnos con display por I2C
#include <Keypad.h>
#include <Adafruit_FT6206.h>
#include <ESP32Servo.h>

#define TAM_DNI 8
#define PASS_TEST "1234567"
#define NAME_TEST "Nicolas"

#define PUERTA_ABIERTA 100
#define PUERTA_CERRADA 0

#define FRECUENCIA_PITIDO 1000
#define DURACION_PITIDO 500

// Definición del pin del buzzer
const int BUZZER_PIN = 4;

//Display LCD
//conectado al GPIO 22 y 21
const int SCL_PIN = 22;
const int SDA_PIN = 21;


const byte ADDR_DISPLAY = 0x27; //Dirección de display

LiquidCrystal_I2C lcd(ADDR_DISPLAY, 16, 2);   //Hay que decirles 16 columnas y 2 filas.

//Sensor Touch
Adafruit_FT6206 touch = Adafruit_FT6206();
const int TOUCH_PIN = 21;
const byte ADDR_TOUCH = 0x38; //Dirección de touch

// Servo
const int SERVO_PIN = 15;
int posicionServo = PUERTA_CERRADA;
Servo servo;

//Keypad
const uint8_t ROWS = 4;
const uint8_t COLS = 3;

char keys[ROWS][COLS] = {
  { '1', '2', '3' },
  { '4', '5', '6' },
  { '7', '8', '9' },
  { '*', '0', '#' }
};

uint8_t colPins[COLS] = { 26, 25, 33 }; // Pins connected to C1, C2, C3
uint8_t rowPins[ROWS] = { 13, 12, 14, 27 }; // Pins connected to R1, R2, R3, R4

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

char passKeypad[TAM_DNI + 1]; //El +1 refiere al \0
int idxPassKeypad = 0;

//LED
const int LED_PIN = 19;
const int RED_LED = 5;

void setup() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.begin(115200); //Debug display.
  Serial.begin(9600);     //Debug keypad
  pinMode(LED_PIN, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  lcd.init();
  lcd.backlight();

  touch.begin();

  servo.attach(SERVO_PIN, 500, 2400);
  cerrarServo();
  pinMode(BUZZER_PIN, OUTPUT);
  
  // Emitir un pitido de 1000 Hz durante 500 milisegundos al encender
  hacerPitido(1000, 500);
}

void loop()
{
  char key = keypad.getKey();
  
  if (touch.touched()) {
    TS_Point p = touch.getPoint();
    Serial.print("X: ");
    Serial.print(p.x);
    Serial.print(" | Y: ");
    Serial.println(p.y);

    digitalWrite(LED_PIN, HIGH);

    if(posicionServo == PUERTA_ABIERTA){
        cerrarServo();
    }else{
      abrirServo();
    }
  
  } else {
    digitalWrite(LED_PIN, LOW);
  }


  if (key != NO_KEY)
  {
    Serial.println(key);

    switch (key) {
      case '*':
        //El caso de borrar
        lcd.clear();
        clearPasskeypad();
        idxPassKeypad =  0;
        break;
      case '#':
        //Acá iría la lógica de bbdd
        //Por el momento voy a poner la lógica de comparar con la pass de test
        lcd.clear();

        passKeypad[++idxPassKeypad] = '\0';
        idxPassKeypad = 0;

        if (compareKeys(passKeypad, PASS_TEST))
        {
          lcd.print(passKeypad);
          lcd.setCursor(0, 1);
          lcd.print(NAME_TEST);
          digitalWrite(LED_PIN, HIGH);
          abrirServo();
          aperturaPuerta();
        }
        else 
        {
          lcd.print("DNI Incorrecto");
          digitalWrite(RED_LED, HIGH);
          delay(3000);
          digitalWrite(RED_LED, LOW);
        }
        
        delay(1500);


        digitalWrite(LED_PIN, LOW);
        lcd.clear();
        clearPasskeypad();

        if (posicionServo == PUERTA_ABIERTA)
        {
          cerrarServo();
        }
        break;
      default:
        passKeypad[idxPassKeypad] = key;

        lcd.setCursor(0, 0);
        lcd.print(passKeypad);


        if (idxPassKeypad == TAM_DNI)
        {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("DNI Incorrecto");

          delay(3000);
          lcd.clear();
          clearPasskeypad();
        } 
        else 
          idxPassKeypad++;
        break;
    }
  }
}

void clearPasskeypad() 
{
  unsigned int indexClear = 0;
  while (indexClear <= TAM_DNI)
  {
    passKeypad[indexClear++] = '\0';
  }
}

boolean compareKeys(char* keypass, char* keytest)
{
  for (unsigned int i = 0; i < TAM_DNI - 1; i++)
  {
    if (*(keypass + i) != *(keytest + i))
        return false;
  }
  return true;
}

void cerrarServo() {
  for (int i = posicionServo; i >= PUERTA_CERRADA; i--) {
    servo.write(i);
    delay(15);
  }
  posicionServo = PUERTA_CERRADA; // Forzamos el valor exacto al terminar
}

void abrirServo() {
  for (int i = posicionServo; i <= PUERTA_ABIERTA; i++) {
    servo.write(i);
    delay(15);
  }
  posicionServo = PUERTA_ABIERTA; // Forzamos el valor exacto al terminar
}

int aperturaPuerta() 
{
  for(int i = 10; i >= 0; i--)
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Tiempo restante:");
    lcd.setCursor(0, 1);
    lcd.print(i);
    delay(1000);
    if(i <= 5){

      hacerPitido(FRECUENCIA_PITIDO,DURACION_PITIDO);
    }
  }
  cerrarServo();
  return 1;
}

// Función auxiliar para emitir un pitido
void hacerPitido(int frecuencia, int duracionMs) {
  tone(BUZZER_PIN, frecuencia, duracionMs);
}
