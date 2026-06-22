/*
  ===================================================================
  PROGETTO: Sonificazione di una serra - SKETCH COMBINATO STABILE
  SCHEDA: Arduino UNO R4 WiFi
  NOTE: Max/MSP continuo (2s) + Storico auto-pulente attivo (8gg / 20 min)
        Ordinamento cronologico corretto tramite padding degli zeri.
  ===================================================================
*/

// --- LIBRERIE ---
#include <DHT11.h>          // Include la libreria per leggere il sensore di temperatura e umidità dell'aria DHT11
#include <Firebase.h>       // Include la libreria per comunicare con il database Firebase Realtime Database
#include <WiFiUdp.h>        // Include la libreria per gestire i pacchetti UDP, necessari per la richiesta dell'orario
#include <NTPClient.h>      // Include la libreria per connettersi ai server NTP e ottenere l'ora esatta da Internet
#include "secrets.h"        // Include il file esterno contenente le credenziali Wi-Fi (SSID, Password) e l'URL di Firebase

// --- CONFIGURAZIONE PIN ---
DHT11 dht11(2);             // Inizializza il sensore DHT11 associandolo al pin digitale 2 di Arduino
#define GROUND_ANALOG_PIN A0 // Definisce il pin analogico A0 per la lettura dell'umidità del terreno
const int GROUND_VCC_PIN = 7;    // Imposta il pin digitale 7 per alimentare l'igrometro del terreno solo quando serve
const int MQ135_PIN = A1;   // Imposta il pin analogico A1 per ricevere il segnale del sensore di qualità dell'aria MQ135
const int LIGHT_PIN = A2;   // Imposta il pin analogico A2 per leggere il valore della fotoresistenza (luce)
const int RAIN_ANALOG_PIN = A3;  // Imposta il pin analogico A3 per misurare la quantità di pioggia sul sensore
const int RAIN_DIGITAL_PIN = 4;  // Imposta il pin digitale 4 per rilevare la presenza/assenza di pioggia (soglia ON/OFF)
const int RAIN_VCC_PIN = 5;      // Imposta il pin digitale 5 per alimentare il sensore di pioggia solo quando serve

// --- CONFIGURAZIONE ORARIO (NTP) ---
WiFiUDP ntpUDP;             // Crea l'oggetto UDP necessario alla libreria NTPClient per mandare pacchetti in rete
// Configura il client NTP: usa il server europeo, imposta il fuso a +2 ore (7200 secondi per l'ora legale) e aggiorna ogni minuto (60000 ms)
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 7200, 60000);

// --- TIMERS ---
unsigned long ultimoInvioMax = 0;       // Variabile per memorizzare l'ultimo millisecondo in cui abbiamo inviato i dati a Max/MSP
const unsigned long intervalloMax = 2000;       // Intervallo di tempo costante fissato a 2 secondi (2000 millisecondi) per Max/MSP

unsigned long ultimoInvioStorico = 0;   // Variabile per memorizzare l'ultimo millisecondo in cui abbiamo scritto lo storico su Firebase
const unsigned long intervalloStorico = 1200000; // Intervallo di tempo costante fissato a 20 minuti (1.200.000 millisecondi) per lo storico

// --- INIZIALIZZAZIONE FIREBASE ---
Firebase fb(REFERENCE_URL); // Inizializza il modulo Firebase passandogli l'URL del tuo database scritto in secrets.h

void setup() { // Funzione di configurazione iniziale, eseguita una sola volta all'avvio di Arduino
    Serial.begin(115200);   // Apre la comunicazione seriale con il computer alla velocità di 115200 baud per i log di debug

    // Configurazione della modalità dei PIN di alimentazione controllata e dei pin digitali
    pinMode(GROUND_VCC_PIN, OUTPUT);    // Imposta il pin dell'alimentazione del terreno come OUTPUT (manderà corrente)
    pinMode(RAIN_VCC_PIN, OUTPUT);      // Imposta il pin dell'alimentazione della pioggia come OUTPUT (manderà corrente)
    digitalWrite(GROUND_VCC_PIN, LOW);   // Spegne subito l'alimentazione del terreno per evitare elettrolisi precoce
    digitalWrite(RAIN_VCC_PIN, LOW);     // Spegne subito l'alimentazione del sensore pioggia per preservarlo dalla corrosione
    pinMode(RAIN_ANALOG_PIN, INPUT);    // Configura il pin analogico della pioggia come ingresso dati
    pinMode(RAIN_DIGITAL_PIN, INPUT);   // Configura il pin digitale della pioggia come ingresso dati

    // Connessione Wi-Fi
    Serial.println(); // Stampa una riga vuota sul monitor seriale
    Serial.print("Connessione a: "); Serial.println(WIFI_SSID); // Stampa a quale rete Wi-Fi si sta tentando l'aggancio
    
    // Gestione reti aperte vs reti protette
    if (String(WIFI_PASSWORD).length() == 0) {
        // Se la password è vuota nel file secrets.h, avvia la connessione per reti aperte
        WiFi.begin(WIFI_SSID); 
    } else {
        // Se è presente una password, usa la configurazione standard protetta
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD); 
    }

    while (WiFi.status() != WL_CONNECTED) { // Ciclo che continua a girare all'infinito finché lo stato del Wi-Fi non è "CONNESSO"
        delay(500);         // Aspetta mezzo secondo tra un controllo e l'altro per non sovraccaricare il chip Wi-Fi
        Serial.print("-");  // Stampa un trattino sulla seriale per mostrare visivamente l'attesa del segnale
    }
    Serial.println("\nWi-Fi Connesso!"); // Segnala sul monitor seriale che la connessione è avvenuta con successo

    // Sincronizzazione iniziale dell'orario
    timeClient.begin();     // Inizializza il client NTP per farlo dialogare con i server di rete
    timeClient.update();    // Invia una richiesta al server e scarica l'Epoch Time (i secondi trascorsi dal 1970) aggiornato
    Serial.println("Orologio di rete (NTP) Pronto."); // Conferma sul monitor seriale che l'orario di rete è sincronizzato
    
    Serial.println("Sistema Combinato Pronto.");     // Conferma che l'avvio è terminato e Arduino sta per passare al loop principale
}

void loop() { // Funzione principale che viene eseguita continuamente in ciclo infinito alla massima velocità della CPU
    unsigned long tempoCorrente = millis(); // Cattura i millisecondi trascorsi dall'accensione di Arduino e li salva in tempoCorrente

    // Controllo di sicurezza per la connessione Wi-Fi
    if (WiFi.status() != WL_CONNECTED) { // Se per qualsiasi motivo la connessione Wi-Fi è caduta...
        if (String(WIFI_PASSWORD).length() == 0) {
            WiFi.begin(WIFI_SSID);
        } else {
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
        int t = 0; while (WiFi.status() != WL_CONNECTED && t < 10) { delay(500); t++; } // Prova a riconnettersi per un massimo di 5 secondi
    }

    // =================================================================
    // TIMER 1: AGGIORNAMENTO CONTINUO PER MAX/MSP (OGNI 2 SECONDI)
    // =================================================================
    if (tempoCorrente - ultimoInvioMax >= intervalloMax) { // Controlla se la differenza tra il tempo attuale e l'ultimo invio è di almeno 2000 ms
        ultimoInvioMax = tempoCorrente; // Aggiorna il timestamp dell'ultimo invio per far ripartire da zero il conteggio dei 2 secondi

        int temperature = 0, humidity = 0; // Dichiarazione delle variabili intere per ospitare i dati del DHT11
        dht11.readTemperatureHumidity(temperature, humidity); // Dice alla libreria di leggere il sensore e riempire le due variabili appena create

        digitalWrite(GROUND_VCC_PIN, HIGH); // Alza a 5V il pin 7 per dare corrente al sensore di umidità del terreno
        delay(30);                          // Aspetta 30 millisecondi affinché la corrente si stabilizzi nel terreno
        int groundValAnalog = analogRead(GROUND_ANALOG_PIN); // Legge il valore di umidità della terra dal pin analogico A0
        digitalWrite(GROUND_VCC_PIN, LOW);   // Abbassa a 0V il pin 7 spegnendo il sensore per arrestare l'elettrolisi distruttiva delle piste

        int rawAir = analogRead(MQ135_PIN); // Legge il valore analogico del gas/fumo nell'aria dal pin A1

        digitalWrite(RAIN_VCC_PIN, HIGH); // Alza a 5V il pin 5 per accendere elettricamente la piastra del sensore di pioggia
        delay(30);                        // Aspetta 30 millisecondi per stabilizzare l'alimentazione sul circuito di pioggia
        int rainAnalog = analogRead(RAIN_ANALOG_PIN); // Legge il valore analogico quantitativo della pioggia dal pin A3
        digitalWrite(RAIN_VCC_PIN, LOW);    // Rispegne il sensore di pioggia per limitare i fenomeni di ossidazione accelerata

        float lux = analogRead(LIGHT_PIN) * 0.9765625; // Legge il pin A2 e moltiplica il valore per convertire i passi del convertitore ADC in Lux approssimativi

        if (WiFi.status() == WL_CONNECTED) { // Se siamo correttamente connessi ad internet procediamo all'invio
            Serial.println(">>> Aggiornamento Max/MSP (2s) <<<"); // Mostra sul monitor seriale che sta partendo la trasmissione a 2 secondi
            fb.setInt("data/temperature", temperature); delay(10); // Invia il dato temperatura al nodo fisso di Firebase e attende 10 ms
            fb.setInt("data/humidity", humidity); delay(10);       // Invia l'umidità dell'aria e attende 10 ms
            fb.setInt("data/groundhumidity", groundValAnalog); delay(10); // Invia l'umidità del terreno e attende 10 ms
            fb.setInt("data/aircondition", rawAir); delay(10);     // Invia l'indice di qualità dell'aria e attende 10 ms
            fb.setInt("data/rainAnalog", rainAnalog); delay(10);   // Invia il valore analogico della pioggia e attende 10 ms
            fb.setFloat("data/lightLux", lux);                     // Invia il valore dei Lux (essendo float, usa fb.setFloat) al database senza delay finale
        }
    }

    // =================================================================
    // TIMER 2: INVIO PACCHETTO STORICO CON DATA E ORA (OGNI 20 MINUTI)
    // =================================================================
    if (tempoCorrente - ultimoInvioStorico >= intervalloStorico || ultimoInvioStorico == 0) { // Verifica se sono passati 20 minuti o se è la prima volta assoluta dal boot
        ultimoInvioStorico = tempoCorrente; // Salva il momento attuale per bloccare il timer storico e far ripartire il conto alla rovescia di 20 minuti

        int temperature = 0, humidity = 0; // Configura le variabili locali per la misura della temperatura e umidità dello storico
        dht11.readTemperatureHumidity(temperature, humidity); // Esegue la lettura fisica dei dati dal sensore dell'aria DHT11

        digitalWrite(GROUND_VCC_PIN, HIGH); // Alimenta il sensore del terreno per la sessione di lettura dello storico
        delay(30);                          // Lascia passare 30 ms per eliminare i disturbi elettrici iniziali sul sensore
        int groundValAnalog = analogRead(GROUND_ANALOG_PIN); // Acquisisce l'umidità del terreno dal pin analogico A0
        digitalWrite(GROUND_VCC_PIN, LOW);   // Toglie immediatamente tensione al sensore del terreno per proteggerlo

        int rawAir = analogRead(MQ135_PIN); // Interroga il pin analogico A1 del sensore MQ135 per campionare lo stato dell'aria attuale

        digitalWrite(RAIN_VCC_PIN, HIGH); // Alimenta il modulo della pioggia alzando il pin 5 a 5V
        delay(30);                        // Fine micro-attesa di assestamento della tensione sul circuito di pioggia
        int rainAnalog = analogRead(RAIN_ANALOG_PIN); // Cattura il dato analogico (quantità d'acqua accumulata) della pioggia
        int rainDigital = digitalRead(RAIN_DIGITAL_PIN); // Cattura lo stato digitale (0 = sta piovendo, 1 = asciutto) dal pin digitale 4
        digitalWrite(RAIN_VCC_PIN, LOW);    // Toglie l'alimentazione al circuito pioggia per bloccare l'ossidazione galvanica

        float lux = analogRead(LIGHT_PIN) * 0.9765625; // Campiona la luce ambientale convertendo la lettura analogica in valore Lux float

        if (WiFi.status() == WL_CONNECTED) { // Se la rete Wi-Fi è attiva aggiorniamo l'orologio atomico prima della stringa
            timeClient.update(); // Interroga nuovamente il server NTP per allineare l'orologio al secondo esatto, scongiurando sfasamenti
        }

        unsigned long epochTimeAttuale = timeClient.getEpochTime(); // Estrae l'Epoch Time (i secondi totali dal 1970) calcolato dal timeClient
        int ora = timeClient.getHours(); // Estrae direttamente l'ora attuale (0-23) sfruttando la funzione nativa di NTPClient
        int minutes = timeClient.getMinutes(); // Estrae direttamente il minuto corrente (0-59) sfruttando la funzione nativa di NTPClient

        // =============================================================
        // FASE 1: CALCOLO E CANCELLAZIONE DEL GIORNO DI 8 GIORNI FA
        // =============================================================
        // Sottraiamo i secondi contenuti in 8 giorni esatti (8gg * 24ore * 3600sec = 691200 secondi)
        unsigned long epochPassato = epochTimeAttuale - 691200L;

        // Applichiamo l'algoritmo di Hinnant sull'epoch passato per ricavare la data vecchia da ripulire
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

        if (WiFi.status() == WL_CONNECTED) {
            // Formattiamo anche la data passata con due cifre per rispecchiare fedelmente i nuovi nodi del database
            String mesePassatoStr = (mP < 10) ? "0" + String(mP) : String(mP);
            String giornoPassatoStr = (dP < 10) ? "0" + String(dP) : String(dP);
            
            // Costruiamo il percorso del giorno vecchio da spazzare via (es: storico/2026-06-07)
            String pathVecchio = "storico/" + String(annoPassato) + "-" + mesePassatoStr + "-" + giornoPassatoStr;
            Serial.print(">>> Pulizia Database: Rimosso nodo scaduto di 8 giorni fa: "); Serial.println(pathVecchio);
            
            fb.remove(pathVecchio); // Cancella l'intero nodo del passato liberando spazio su Firebase
            delay(20); // Pausa di assestamento per la transazione sul database cloud
        }

        // =============================================================
        // FASE 2: CALCOLO DATA ATTUALE E INVIO NUOVO PACCHETTO STORICO
        // =============================================================
        // INIZIO ALGORITMO DI HOWARD HINNANT PER IL CALCOLO DI ANNO-MESE-GIORNO DALL'EPOCH TIME CORRENTE
        unsigned long rawDays = epochTimeAttuale / 86400L; // Divide i secondi totali per i secondi contenuti in un giorno (86400) per trovare i giorni totali dal 1970
        long conceptualSeconds = (rawDays * 86400L) + 43200L; // Calcola un punto temporale fittizio a metà giornata per evitare errori di arrotondamento delle ore
        long long daysSinceEpoch = conceptualSeconds / 86400L; // Ricalcola i giorni puliti trascorsi dall'epoca Unix del 1970
        long long zeroDay = daysSinceEpoch + 719468L; // Sposta lo zero temporale di riferimento all'anno 0 del calendario Gregoriano invece del 1970
        long long era = (zeroDay >= 0 ? zeroDay : zeroDay - 146096L) / 146097L; // Determina in quale macro-era di 400 anni ci troviamo (sistema dei cicli bisestili gregoriani)
        unsigned long doe = static_cast<unsigned long>(zeroDay - era * 146097L); // Trova il giorno esatto all'interno dell'era di 400 anni corrente (Day Of Era)
        unsigned long yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365; // Estrae matematicamente l'anno specifico all'interno di quell'era considerando i bisestili (Year Of Era)
        long long y = static_cast<long long>(yoe) + era * 400L; // Ricompone l'anno assoluto combinando l'era di appartenenza e l'anno dell'era trovato
        unsigned long doy = doe - (365*yoe + yoe/4 - yoe/100); // Calcola il giorno dell'anno corrente in cui ci troviamo (Day Of Year, da 0 a 365)
        unsigned long mp = (5*doy + 2)/153; // Trova un mese concettuale partendo da marzo tramite l'algoritmo matematico di Hinnant
        
        unsigned long d = doy - (153*mp+2)/5 + 1; // Calcola il giorno del mese reale
        unsigned long m = mp + (mp < 10 ? 3 : -9); // Calcola il mese reale (1-12)
        long anno = y + (m <= 2 ? 1 : 0); // Calcola l'anno reale correggendo i mesi di Gennaio/Febbraio

        // --- FORMATTAZIONE ORARIO E DATA CON ZERO INIZIALE (PADDING) ---
        String oraStr = (ora < 10) ? "0" + String(ora) : String(ora);
        String minutiStr = (minutes < 10) ? "0" + String(minutes) : String(minutes);
        String meseStr = (m < 10) ? "0" + String(m) : String(m);
        String giornoStr = (d < 10) ? "0" + String(d) : String(d);

        // --- INVIO DEI DATI DELLO STORICO A FIREBASE ---
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println(">>> Scrittura Storico su Firebase (20 min) <<<");
            
            // Creiamo il percorso dinamico ordinabile, es: /storico/2026-06-15/17-00/
            String path = "storico/" + String(anno) + "-" + meseStr + "-" + giornoStr + "/" + oraStr + "-" + minutiStr + "/";
            
            fb.setInt(path + "temperature", temperature); delay(10);
            fb.setInt(path + "humidity", humidity); delay(10);
            fb.setInt(path + "groundhumidity", groundValAnalog); delay(10);
            fb.setInt(path + "aircondition", rawAir); delay(10);
            fb.setInt(path + "rainAnalog", rainAnalog); delay(10);
            fb.setInt(path + "rainDigital", rainDigital); delay(10);
            fb.setFloat(path + "lightLux", lux);
        }
    }
}