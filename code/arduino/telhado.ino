#include <Wire.h>
#include <RTClib.h>
#include <SD.h>
#include <SPI.h>

RTC_DS1307 rtc;
File dataFile; 
File logFile; 
File lastTimeFile; 
File pumpTimeFile;   // <- NOVO: armazena último acionamento da bomba

// Definição dos Pinos:
const int pinSS = 10; 
const int MoisturePin = A0; 
const int Temp1Pin = A1;    
const int Temp2Pin = A2;    

// Variáveis
int start_minute;
int update_interval = 5;
float moisture;
float temp1;
float temp2;

// LEDs
const int ErrorLED_RTC_Pin = 9;
const int ErrorLED_SD_Pin = 8;

// Controle RTC
bool rtc_error = false;
unsigned long last_unix_time = 0;

// ----------- CONTROLE DA BOMBA -----------
const int PumpPin = 7;
unsigned long last_pump_time = 0;     // Unix timestamp do último acionamento
const unsigned long interval_1day = 1UL * 24UL * 3600UL; // 1 dia em segundos
bool pump_running = false;
unsigned long pump_start_millis = 0;
// -----------------------------------------

void setup() {
  Serial.begin(9600);
  Wire.begin();

  // LEDs
  pinMode(ErrorLED_RTC_Pin, OUTPUT); 
  pinMode(ErrorLED_SD_Pin, OUTPUT);
  digitalWrite(ErrorLED_RTC_Pin, LOW); 
  digitalWrite(ErrorLED_SD_Pin, LOW);

  // PINO DO RELÉ
  pinMode(PumpPin, OUTPUT);
  digitalWrite(PumpPin, HIGH);  // HIGH = desligado

  // RTC
  if (!rtc.begin()) {
    Serial.println(F("Não foi possível encontrar o módulo RTC!"));
    while (1);
  }

  if (!rtc.isrunning()) {
    Serial.println(F("RTC não está funcionando! Configurando a hora..."));
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  DateTime now = rtc.now(); 

  // Inicialização SD
  pinMode(pinSS, OUTPUT);
  digitalWrite(pinSS, HIGH);
  delay(500);

  digitalWrite(ErrorLED_SD_Pin, HIGH);
  if (!SD.begin(pinSS)) {
    Serial.println(F("Falha na inicialização do SD Card."));
    while (1) {
      digitalWrite(ErrorLED_SD_Pin, HIGH); delay(100);
      digitalWrite(ErrorLED_SD_Pin, LOW); delay(100);
    }
  }
  Serial.println(F("SD Card pronto."));
  digitalWrite(ErrorLED_SD_Pin, LOW);


  // LAST.TXT (checagem RTC)
  if (SD.exists("LAST.TXT")) {
      lastTimeFile = SD.open("LAST.TXT", FILE_READ);
      if (lastTimeFile) {
          last_unix_time = lastTimeFile.parseInt();
          lastTimeFile.close();
      }
  }

  if (now.unixtime() < last_unix_time && last_unix_time > 0) {
      Serial.println(F("ERRO RTC"));
      rtc_error = true;
      digitalWrite(ErrorLED_RTC_Pin, HIGH);
  }


  // BOMBA.TXT (último acionamento)
  if (SD.exists("BOMBA.TXT")) {
      pumpTimeFile = SD.open("BOMBA.TXT", FILE_READ);
      if (pumpTimeFile) {
          last_pump_time = pumpTimeFile.parseInt();
          pumpTimeFile.close();
      }
  } else {
      last_pump_time = 0;
  }


  // Arquivo dados
  dataFile = SD.open("dados.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.println(F("Timestamp,Umidade,Temp1,Temp2"));
    dataFile.close();
  }

  // Log inicial
  start_minute = now.minute();
  char now_str[20];
  sprintf(now_str, "%04d/%02d/%02d %02d:%02d:%02d",
          now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());

  logFile = SD.open("log.txt", FILE_WRITE); 
  if (logFile) {
    logFile.print(F("Arduino inicializado em: "));
    logFile.println(now_str);
    logFile.close();
  }
}

// ---------------- LOOP -------------------

void loop() {

  if (rtc_error) digitalWrite(ErrorLED_RTC_Pin, HIGH);
  else digitalWrite(ErrorLED_RTC_Pin, LOW);

  DateTime now = rtc.now();
  int now_second = now.second();
  int now_minute = now.minute();
  int now_hour = now.hour();
  int now_day = now.day();
  int now_month = now.month();
  int now_year = now.year();

  unsigned long now_unix = now.unixtime();


  // ----------------- CONTROLE DA BOMBA -----------------

  // Se a bomba está ligada, verificar tempo de desligar
  if (pump_running) {
      if (millis() - pump_start_millis >= 300000UL) { // 5 minutos
          digitalWrite(PumpPin, HIGH);  // Desliga
          pump_running = false;

          logFile = SD.open("log.txt", FILE_WRITE);
          if (logFile) {
              logFile.print("Bomba DESLIGADA em ");
              logFile.print(now_year); logFile.print("/");
              logFile.print(now_month); logFile.print("/");
              logFile.print(now_day); logFile.print(" ");
              logFile.print(now_hour); logFile.print(":");
              logFile.print(now_minute); logFile.print(":");
              logFile.println(now_second);
              logFile.close();
          }
      }
  }

  // Condições para ativação:
  bool pode_ativar =
      (!pump_running) &&
      (moisture < 40.0) &&
      (now_unix - last_pump_time >= interval_1day);

  if (pode_ativar) {
      digitalWrite(PumpPin, LOW);  // Liga (invertido)
      pump_running = true;
      pump_start_millis = millis();
      last_pump_time = now_unix;

      // Salva no SD
      if (SD.exists("BOMBA.TXT")) SD.remove("BOMBA.TXT");
      pumpTimeFile = SD.open("BOMBA.TXT", FILE_WRITE);
      if (pumpTimeFile) {
          pumpTimeFile.print(last_pump_time);
          pumpTimeFile.close();
      }

      // Log
      logFile = SD.open("log.txt", FILE_WRITE);
      if (logFile) {
          logFile.print("Bomba LIGADA em ");
          logFile.print(now_year); logFile.print("/");
          logFile.print(now_month); logFile.print("/");
          logFile.print(now_day); logFile.print(" ");
          logFile.print(now_hour); logFile.print(":");
          logFile.print(now_minute); logFile.print(":");
          logFile.println(now_second);
          logFile.close();
      }
  }

  // ------------------- COLETA E SALVAMENTO -------------------

  if (((now_minute - start_minute) % update_interval == 0) && (now_second >= 30)) {

    moisture = map(analogRead(MoisturePin), 1023, 400, 0.0, 100.0);
    temp1 = analogRead(Temp1Pin)*(500.0 / 1023.0);
    temp2 = analogRead(Temp2Pin)*(500.0 / 1023.0) + 1.5;

    char moisture_str[10];
    char temp1_str[10];
    char temp2_str[10];

    dtostrf(moisture, 6, 2, moisture_str);
    dtostrf(temp1, 6, 2, temp1_str);
    dtostrf(temp2, 6, 2, temp2_str);
    
    char buffer[70];
    sprintf(buffer, "%04d/%02d/%02d %02d:%02d:%02d,%s,%s,%s",
        now_year, now_month, now_day,
        now_hour, now_minute, now_second,
        moisture_str, temp1_str, temp2_str);

    digitalWrite(ErrorLED_SD_Pin, HIGH);

    dataFile = SD.open("dados.txt", FILE_WRITE);
    if (!dataFile) {
        SD.begin(pinSS);
        dataFile = SD.open("dados.txt", FILE_WRITE);
    }

    if (dataFile) {
      dataFile.println(buffer);
      dataFile.close();

      if (SD.exists("LAST.TXT")) SD.remove("LAST.TXT");
      lastTimeFile = SD.open("LAST.TXT", FILE_WRITE);
      if (lastTimeFile) {
        lastTimeFile.print(now_unix);
        lastTimeFile.close();
      }
    }

    digitalWrite(ErrorLED_SD_Pin, LOW);
  }

  delay(1000*30);
}
