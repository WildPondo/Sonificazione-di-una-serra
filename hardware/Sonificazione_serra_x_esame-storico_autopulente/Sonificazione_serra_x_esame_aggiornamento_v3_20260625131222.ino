/*
  ===================================================================
  PROGETTO: Sonificazione di una serra - SKETCH OTTIMIZZATO V1
  SCHEDA: Arduino UNO R4 WiFi
  NOTE: Controllo Wi-Fi ridotto a 1 minuto. Rimossa ridondanza WiFi.status.
  ===================================================================
*/

// --- LIBRERIE ---
#include <DHT11.h>          
#include <Firebase.h>       
#include <WiFiUdp.h>        
#include <NTPClient.h>      
#include "secrets.h"        

// --- CONFIGURAZIONE PIN ---
DHT11 dht11(2);             
#define GROUND_ANALOG_PIN A0 
const int GROUND_VCC_PIN = 7;    
const int MQ135_PIN = A1;   
const int LIGHT_PIN = A2;   
const int RAIN_ANALOG_PIN = A3;  
const int RAIN_DIGITAL_PIN = 4;  
const int RAIN_VCC_PIN = 5;      

// --- CONFIGURAZIONE ORARIO (NTP) ---
WiFiUDP ntpUDP;             
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 7200, 60000);

// --- TIMERS ---
unsigned long ultimoInvioMax = 0;       
const unsigned long intervalloMax = 2000;       

unsigned long ultimoInvioStorico = 0;   
const unsigned long intervalloStorico = 1200000; 

// NUOVO: Timer per il controllo della connessione
unsigned long ultimoControlloWiFi = 0;
const unsigned long intervalloWiFi = 60000; // 1 minuto

// --- INIZIALIZZAZIONE FIREBASE ---
Firebase fb(REFERENCE_URL); 

void setup() { 
    Serial.begin(115200);   

    pinMode(GROUND_VCC_PIN, OUTPUT);    
    pinMode(RAIN_VCC_PIN, OUTPUT);      
    digitalWrite(GROUND_VCC_PIN, LOW);   
    digitalWrite(RAIN_VCC_PIN, LOW);     
    pinMode(RAIN_ANALOG_PIN, INPUT);    
    pinMode(RAIN_DIGITAL_PIN, INPUT);   

    Serial.println(); 
    Serial.print("Connessione a: "); Serial.println(WIFI_SSID); 
    
    if (String(WIFI_PASSWORD).length() == 0) {
        WiFi.begin(WIFI_SSID); 
    } else {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD); 
    }

    while (WiFi.status() != WL_CONNECTED) { 
        delay(500);         
        Serial.print("-");  
    } 
    Serial.println("\nWi-Fi Connesso!"); 

    timeClient.begin();     
    timeClient.update();    
    Serial.println("Orologio di rete (NTP) Pronto."); 
    Serial.println("Sistema Combinato Pronto.");     
}

void loop() { 
    unsigned long tempoCorrente = millis(); 

    // =================================================================
    // CONTROLLO TEMPORIZZATO DELLA CONNESSIONE WI-FI (OGNI 1 MINUTO)
    // =================================================================
    if (tempoCorrente - ultimoControlloWiFi >= intervalloWiFi) {
        ultimoControlloWiFi = tempoCorrente; 

        if (WiFi.status() != WL_CONNECTED) { 
            Serial.println("Warning: Wi-Fi disconnesso. Tentativo di riconnessione...");
            if (String(WIFI_PASSWORD).length() == 0) {
                WiFi.begin(WIFI_SSID);
            } else {
                WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            }
            
            int t = 0; 
            while (WiFi.status() != WL_CONNECTED && t < 10) { delay(500); t++; } 
            
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("Wi-Fi Riconnesso con successo!");
            } else {
                Serial.println("Riconnessione fallita. Ci riproverò tra un minuto.");
            }
        }
    }

    // =================================================================
    // TIMER 1: AGGIORNAMENTO CONTINUO PER MAX/MSP (OGNI 2 SECONDI)
    // =================================================================
    if (tempoCorrente - ultimoInvioMax >= intervalloMax) { 
        ultimoInvioMax = tempoCorrente; 

        int temperature = 0, humidity = 0; 
        dht11.readTemperatureHumidity(temperature, humidity); 

        digitalWrite(GROUND_VCC_PIN, HIGH); 
        delay(30);                          
        int groundValAnalog = analogRead(GROUND_ANALOG_PIN); 
        digitalWrite(GROUND_VCC_PIN, LOW);   

        int rawAir = analogRead(MQ135_PIN); 

        digitalWrite(RAIN_VCC_PIN, HIGH); 
        delay(30);                        
        int rainAnalog = analogRead(RAIN_ANALOG_PIN); 
        digitalWrite(RAIN_VCC_PIN, LOW);    

        float lux = analogRead(LIGHT_PIN) * 0.9765625; 

        Serial.println(">>> Aggiornamento Max/MSP (2s) <<<"); 

        // Costruiamo la stringa JSON unica con tutti i dati
        String jsonMax = "{";
        jsonMax += "\"temperature\":" + String(temperature) + ",";
        jsonMax += "\"humidity\":" + String(humidity) + ",";
        jsonMax += "\"groundhumidity\":" + String(groundValAnalog) + ",";
        jsonMax += "\"aircondition\":" + String(rawAir) + ",";
        jsonMax += "\"rainAnalog\":" + String(rainAnalog) + ",";
        jsonMax += "\"lightLux\":" + String(lux, 2); // Mantieni 2 cifre decimali
        jsonMax += "}";

        // Unica chiamata di rete! Puntiamo direttamente al nodo "data"
        fb.setJson("data", jsonMax);                     
    }

    // =================================================================
    // TIMER 2: INVIO PACCHETTO STORICO CON DATA E ORA (OGNI 20 MINUTI)
    // =================================================================
    if (tempoCorrente - ultimoInvioStorico >= intervalloStorico || ultimoInvioStorico == 0) { 
        ultimoInvioStorico = tempoCorrente; 

        // UNICO CONTROLLO DI SICUREZZA: Eseguiamo lo storico solo se siamo online
        if (WiFi.status() == WL_CONNECTED) {

            int temperature = 0, humidity = 0; 
            dht11.readTemperatureHumidity(temperature, humidity); 

            digitalWrite(GROUND_VCC_PIN, HIGH); 
            delay(30);                          
            int groundValAnalog = analogRead(GROUND_ANALOG_PIN); 
            digitalWrite(GROUND_VCC_PIN, LOW);   

            int rawAir = analogRead(MQ135_PIN); 

            digitalWrite(RAIN_VCC_PIN, HIGH); 
            delay(30);                        
            int rainAnalog = analogRead(RAIN_ANALOG_PIN); 
            int rainDigital = digitalRead(RAIN_DIGITAL_PIN); 
            digitalWrite(RAIN_VCC_PIN, LOW);    

            float lux = analogRead(LIGHT_PIN) * 0.9765625; 

            // Sincronizzazione orario sicura
            timeClient.update(); 

            unsigned long epochTimeAttuale = timeClient.getEpochTime(); 
            int ora = timeClient.getHours(); 
            int minutes = timeClient.getMinutes(); 

            // =============================================================
            // FASE 1: CALCOLO E CANCELLAZIONE DEL GIORNO DI 8 GIORNI FA
            // =============================================================
            unsigned long epochPassato = epochTimeAttuale - 691200L;

            unsigned long rawDaysP = epochPassato / 86400L; 
            long conceptualSecondsP = (rawDaysP * 86400L) + 43200L; 
            long long daysSinceEpochP = conceptualSecondsP / 86400L; 
            long long zeroDayP = daysSinceEpochP + 719468L; 
            long long eraP = (zeroDayP >= 0 ? zeroDayP : zeroDayP - 146096L) / 146097L; 
            unsigned long doeP = static_cast<unsigned long>(zeroDayP - eraP * 146097L); 
            unsigned long yoeP = (doeP - doeP/1460 + doeP/36524 - doeP/146096) / 365; 
            long long yP = static_cast<long long>(yoeP) + eraP * 400L; 
            unsigned long doyP = doeP - (365*yoeP + yoeP/4 - yoeP/100); 
            unsigned long mpP = (5*doyP + 2)/153; 
            unsigned long dP = doyP - (153*mpP+2)/5 + 1; 
            unsigned long mP = mpP + (mpP < 10 ? 3 : -9); 
            long annoPassato = yP + (mP <= 2 ? 1 : 0); 

            String mesePassatoStr = (mP < 10) ? "0" + String(mP) : String(mP);
            String giornoPassatoStr = (dP < 10) ? "0" + String(dP) : String(dP);
            
            String pathVecchio = "storico/" + String(annoPassato) + "-" + mesePassatoStr + "-" + giornoPassatoStr;
            Serial.print(">>> Pulizia Database: Rimosso nodo scaduto di 8 giorni fa: "); Serial.println(pathVecchio);
            
            fb.remove(pathVecchio); 
            delay(20); 

            // =============================================================
            // FASE 2: CALCOLO DATA ATTUALE E INVIO NUOVO PACCHETTO STORICO
            // =============================================================
            unsigned long rawDays = epochTimeAttuale / 86400L; 
            long conceptualSeconds = (rawDays * 86400L) + 43200L; 
            long long daysSinceEpoch = conceptualSeconds / 86400L; 
            long long zeroDay = daysSinceEpoch + 719468L; 
            long long era = (zeroDay >= 0 ? zeroDay : zeroDay - 146096L) / 146097L; 
            unsigned long doe = static_cast<unsigned long>(zeroDay - era * 146097L); 
            unsigned long yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365; 
            long long y = static_cast<long long>(yoe) + era * 400L; 
            unsigned long doy = doe - (365*yoe + yoe/4 - yoe/100); 
            unsigned long mp = (5*doy + 2)/153; 
            
            unsigned long d = doy - (153*mp+2)/5 + 1; 
            unsigned long m = mp + (mp < 10 ? 3 : -9); 
            long anno = y + (m <= 2 ? 1 : 0); 

            String oraStr = (ora < 10) ? "0" + String(ora) : String(ora);
            String minutiStr = (minutes < 10) ? "0" + String(minutes) : String(minutes);
            String meseStr = (m < 10) ? "0" + String(m) : String(m);
            String giornoStr = (d < 10) ? "0" + String(d) : String(d);

            Serial.println(">>> Scrittura Storico su Firebase (20 min) <<<");
            
            String path = "storico/" + String(anno) + "-" + meseStr + "-" + giornoStr + "/" + oraStr + "-" + minutiStr;
            
            // Costruiamo il JSON per lo storico (include anche rainDigital)
            String jsonStorico = "{";
            jsonStorico += "\"temperature\":" + String(temperature) + ",";
            jsonStorico += "\"humidity\":" + String(humidity) + ",";
            jsonStorico += "\"groundhumidity\":" + String(groundValAnalog) + ",";
            jsonStorico += "\"aircondition\":" + String(rawAir) + ",";
            jsonStorico += "\"rainAnalog\":" + String(rainAnalog) + ",";
            jsonStorico += "\"rainDigital\":" + String(rainDigital) + ",";
            jsonStorico += "\"lightLux\":" + String(lux, 2);
            jsonStorico += "}";

            // Unico invio strutturato per lo storico nel punto esatto dell'orario
            fb.setJson(path, jsonStorico);
        } else {
            Serial.println(">>> Storico saltato: Wi-Fi momentaneamente disconnesso <<<");
        }
    }
}