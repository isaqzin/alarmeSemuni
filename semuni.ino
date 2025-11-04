/*
 * -----------------------------------------------------------------------------
 * INCLUSÃO DE BIBLIOTECAS
 * Bibliotecas são "caixas de ferramentas" que dão ao nosso ESP32
 * superpoderes, como se conectar ao Wi-Fi, falar com servidores
 * web (HTTP) e entender dados no formato JSON.
 * -----------------------------------------------------------------------------
 */
#include <WiFi.h>          // Para se conectar a redes Wi-Fi
#include <HTTPClient.h>    // Para fazer requisições web (GET, POST)
#include <ArduinoJson.h>   // Para "ler" e "escrever" no formato JSON (como um dicionário)
#include "time.h"         // Para pegar a hora certa da internet

/*
 * -----------------------------------------------------------------------------
 * CONFIGURAÇÕES DA REDE E SERVIDOR
 * Aqui colocamos os dados para o ESP32 se conectar na sua casa
 * e saber onde está o servidor (computador) que controla o alarme.
 * -----------------------------------------------------------------------------
 */
const char* ssid = "Eletronjun ";                   // Nome da sua rede Wi-Fi
const char* password = "123456789";            // Senha da sua rede Wi-Fi
String serverUrl = "http://192.168.0.119:8080"; // Endereço IP e porta do seu servidor local

/*
 * -----------------------------------------------------------------------------
 * CONFIGURAÇÕES DE HORA (NTP)
 * O ESP32 não tem relógio. Usamos isso para ele perguntar a hora
 * a um "servidor de tempo" na internet (NTP).
 * -----------------------------------------------------------------------------
 */
const char* ntpServer = "pool.ntp.org";      // Endereço do servidor de hora
const long  gmtOffset_sec = -3 * 3600;     // Fuso horário de Brasília (GMT-3), em segundos
const int   daylightOffset_sec = 0;          // Horário de verão (0 = desligado)

/*
 * -----------------------------------------------------------------------------
 * DEFINIÇÃO DOS PINOS (PORTAS FÍSICAS)
 * Dizemos ao ESP32 onde cada componente está conectado.
 * -----------------------------------------------------------------------------
 */
const int LASER_PIN = 13;   // O pino que liga/desliga o módulo laser
const int BUZZER_PIN = 12;  // O pino que faz o buzzer apitar
const int LDR_PIN = 34;     // O pino que lê o sensor de luz (LDR). É um pino de "leitura analógica"

/*
 * -----------------------------------------------------------------------------
 * CONFIGURAÇÕES DO ALARME
 * -----------------------------------------------------------------------------
 */
// Este é o valor "limite" do sensor de luz.
// Se a leitura do LDR for MENOR que isso, significa que o feixe foi cortado.
// Você ajusta esse valor olhando o monitor serial.
int LDR_THRESHOLD = 3000;

/*
 * -----------------------------------------------------------------------------
 * VARIÁVEIS DE CONTROLE (A MEMÓRIA DO PROGRAMA)
 * Usamos variáveis para guardar informações e lembrar o que está acontecendo.
 * -----------------------------------------------------------------------------
 */
String modoAlarme = "indefinido";      // Guarda o modo atual (ex: "ligado", "manual", "desligado")
String previousModoAlarme = "";        // Guarda o modo anterior, para sabermos se algo mudou
unsigned long ultimaSincronizacao = 0; // Guarda "quando" foi a última vez que falamos com o servidor
const long intervalo_sincronizacao = 5000; // Tempo (em milissegundos) para falar com o servidor (5 segundos)

// Variáveis para controlar o disparo
bool disparoReportado = false;      // "true" se já avisamos o servidor sobre um disparo
bool buzzerRemotoAtivo = false;     // "true" se o buzzer foi ativado pelo servidor (modo manual)
unsigned long buzzerRemotoStartTime = 0; // Guarda "quando" o buzzer remoto começou a tocar

/*
 * =============================================================================
 * FUNÇÕES DE COMUNICAÇÃO COM O SERVIDOR (API)
 * Essas funções são as "mensageiras". Elas que vão até o servidor
 * buscar ou entregar informações.
 * =============================================================================
 */

/**
 * Pergunta ao servidor: "Qual é o modo do alarme agora?"
 * Esta função envia um pedido (GET) para o servidor na rota "/obter-estado-alarme/".
 * O servidor responde com um JSON (ex: {"estado": "ligado"}).
 * A função então lê essa resposta e salva em 'modoAlarme'.
 */
void obter_estado_alarme() {
  HTTPClient http;
  String url = serverUrl + "/obter-estado-alarme/";
  
  http.begin(url);  
  http.addHeader("Accept", "application/json");

  int httpCode = http.GET(); // Faz a pergunta (GET)

  if (httpCode > 0) { // Se o servidor respondeu...
    String payload = http.getString(); // Pega a resposta
    
    JsonDocument doc; // Prepara um "leitor" de JSON
    deserializeJson(doc, payload); // Tenta ler a resposta

    if (!doc["estado"].isNull()) { // Se conseguiu ler e a chave "estado" existe...
      modoAlarme = doc["estado"].as<String>(); // Atualiza nossa variável!
    }
  }
  http.end(); // Encerra a conexão
}

/**
 * Avisa o servidor: "O alarme disparou!"
 *
 * Esta função envia um aviso (POST) para a rota "/receber-dados/".
 * Ela envia um JSON (ex: {"estado_alarme": "ligado"}) para o servidor
 * saber o que aconteceu.
 */
void enviar_estado_disparo(String estado) {
  HTTPClient http;
  String url = serverUrl + "/receber-dados/";
  http.begin(url);
  http.addHeader("Content-Type", "application/json"); // Avisa que estamos enviando JSON

  JsonDocument doc; // Prepara um "escritor" de JSON
  doc["estado_alarme"] = estado; // Coloca o estado dentro do JSON
  String requestBody;
  serializeJson(doc, requestBody); // Transforma em texto
  
  http.POST(requestBody); // Envia o aviso (POST)
  http.end();
}

/**
 * Pergunta ao servidor: "Devo ligar o buzzer?" (Usado no modo manual)
 *
 * Esta função pergunta (GET) ao servidor na rota "/verificar-buzzer/".
 * O servidor responde (ex: {"disparar_buzzer": true}).
 * Ela retorna 'true' se o servidor mandou ligar, ou 'false' se não.
 */
bool verificar_buzzer_remoto() {
  HTTPClient http;
  String url = serverUrl + "/verificar-buzzer/";
  http.begin(url);
  http.addHeader("Accept", "application/json");
  bool disparar = false; // Começa achando que não é pra disparar

  int httpCode = http.GET();
  if (httpCode > 0) {
    String payload = http.getString();
    JsonDocument doc;
    deserializeJson(doc, payload);
    if(!doc.isNull()){
      disparar = doc["disparar_buzzer"].as<bool>(); // Lê a resposta do servidor
    }
  }
  http.end();
  return disparar; // Retorna a decisão
}

/**
 * Avisa o servidor: "Ok, já toquei o buzzer que você pediu!"
 *
 * Esta função envia um aviso (POST) para "/resetar-buzzer/".
 * Isso faz o servidor "zerar" o pedido de tocar o buzzer, para que
 * ele não toque sem parar toda vez que a função 'verificar_buzzer_remoto' rodar.
 */
void resetar_buzzer_remoto() {
  HTTPClient http;
  String url = serverUrl + "/resetar-buzzer/";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.POST("{}"); // Envia um POST (com um JSON vazio, só para confirmar)
  http.end();
}

/*
 * =============================================================================
 * FUNÇÕES AUXILIARES (PEQUENAS AJUDANTES)
 * =============================================================================
 */

/**
 * Mostra a hora atual no Monitor Serial.
 *
 * Pega a hora que foi sincronizada com a internet e imprime
 * de um jeito fácil de ler (ex: "Segunda, 03/11/2025 22:52:00").
 */
void printLocalTime(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){ // Tenta pegar a hora
    Serial.println("Falha ao obter a hora"); // Avisa se não conseguir
    return;
  }
  // Imprime a hora formatada
  Serial.println(&timeinfo, "Hora: %A, %d/%m/%Y %H:%M:%S");
}

/*
 * =============================================================================
 * SETUP - A PREPARAÇÃO INICIAL
 * Esta função roda APENAS UMA VEZ, quando o ESP32 é ligado.
 * É como "arrumar a casa" antes da festa começar.
 * =============================================================================
 */
void setup() {
  // Inicia a comunicação com o computador (para podermos ler as mensagens no "Monitor Serial")
  Serial.begin(115200);
  delay(1000); // Espera 1 segundo

  // Configura os pinos:
  pinMode(LASER_PIN, OUTPUT);  // O pino do Laser vai "enviar" energia (SAÍDA)
  pinMode(BUZZER_PIN, OUTPUT); // O pino do Buzzer também vai "enviar" energia (SAÍDA)
  pinMode(LDR_PIN, INPUT);     // O pino do LDR vai "receber" informação (ENTRADA)
  
  // Garante que tudo comece desligado.
  // IMPORTANTE: O buzzer é "ativo-baixo", significa que HIGH (alto) DESLIGA
  // e LOW (baixo) LIGA.
  digitalWrite(BUZZER_PIN, HIGH); // Manda sinal ALTO para o buzzer (mantém desligado)
  digitalWrite(LASER_PIN, LOW);   // Manda sinal BAIXO para o laser (mantém desligado)

  // Inicia a conexão Wi-Fi
  WiFi.mode(WIFI_STA); // Define o modo "Estação" (ele vai se conectar a um roteador)
  WiFi.begin(ssid, password); // Tenta se conectar com o usuário e senha

  Serial.print("Conectando a rede ");
  Serial.print(ssid);
  while (WiFi.status() != WL_CONNECTED) { // Enquanto não conectar...
    delay(500); // Espera meio segundo
    Serial.print("."); // Imprime um "." na tela (sinal de espera)
  }
  Serial.println("\nConectado!"); // Avisa que conectou
  Serial.print("Endereço IP: ");
  Serial.println(WiFi.localIP()); // Mostra o IP que o ESP32 pegou
  
  // Sincroniza a hora
  Serial.println("Sincronizando horário via NTP...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  printLocalTime(); // Mostra a hora que acabou de pegar
}

/*
 * =============================================================================
 * LOOP - O CÉREBRO
 * Esta função roda sem parar, para sempre.
 * É aqui que toda a mágica acontece: ler sensores, tomar decisões,
 * acender luzes, tocar sons e falar com o servidor.
 * =============================================================================
 */
void loop() {
  // --- 1. VERIFICAÇÃO DE SAÚDE (Wi-Fi) ---
  if(WiFi.status() != WL_CONNECTED) { // Se, por algum motivo, o Wi-Fi caiu...
    Serial.println("Wi-Fi desconectado. Tentando reconectar...");
    digitalWrite(LASER_PIN, LOW);   // Desliga tudo por segurança
    digitalWrite(BUZZER_PIN, HIGH); // (HIGH desliga o buzzer)
    delay(5000); // Espera 5 segundos
    WiFi.begin(ssid, password); // Tenta reconectar
    return; // Pula o resto do loop e começa de novo
  }

  // --- 2. SINCRONIZAÇÃO COM O SERVIDOR ---
  // Verifica se já passou o tempo de falar com o servidor de novo
  if (millis() - ultimaSincronizacao > intervalo_sincronizacao) {
    obter_estado_alarme(); // Pergunta ao servidor qual é o modo atual
    ultimaSincronizacao = millis(); // Marca a "hora" que fizemos isso
  }

  // --- 3. DETECÇÃO DE MUDANÇA DE MODO ---
  // Se o modo que veio do servidor ("modoAlarme") for diferente do que
  // estava antes ("previousModoAlarme")...
  if (modoAlarme != previousModoAlarme) {
    Serial.print("Modo de alarme alterado para '");
    Serial.print(modoAlarme);
    Serial.println("'");
    
    // Zera tudo para evitar problemas
    digitalWrite(BUZZER_PIN, HIGH); // Desliga o buzzer
    disparoReportado = false;       // Reseta a flag de "já avisei"
    buzzerRemotoAtivo = false;      // Reseta a flag do buzzer remoto

    if (modoAlarme.equals("manual")) {
        // Se entrou no modo manual, manda um reset para o servidor
        // por garantia.
        Serial.println("-> Entrando no modo manual. Resetando estado do buzzer no servidor.");
        resetar_buzzer_remoto();
    }
    previousModoAlarme = modoAlarme; // Atualiza a memória do "modo anterior"
  }

  // --- 4. LÓGICA PRINCIPAL (LEITURA E DECISÃO) ---
  int ldrValue = 0; // Variável para guardar a leitura do LDR

  // --- MODO: LIGADO (Automático) ---
  if (modoAlarme.equals("ligado")) {
    digitalWrite(LASER_PIN, HIGH); // Liga o laser
    ldrValue = analogRead(LDR_PIN); // Lê o sensor de luz

    // Se o feixe foi cortado (luz baixa)...
    if (ldrValue < LDR_THRESHOLD) {
      digitalWrite(BUZZER_PIN, LOW); // LIGA o buzzer (LOW liga)
      
      // Se for a *primeira vez* que detectamos isso...
      if (!disparoReportado) {
        Serial.println("-> ALARME AUTOMÁTICO ATIVADO (LDR baixo)");
        enviar_estado_disparo("ligado"); // Avisa o servidor
        disparoReportado = true; // Marca que "já avisamos"
      }
    } else { // Se o feixe está normal (luz alta)...
      digitalWrite(BUZZER_PIN, HIGH); // DESLIGA o buzzer
      
      // Se o buzzer estava ligado antes...
      if (disparoReportado) {
        Serial.println("-> Alarme automático desativado (LDR normal)");
        disparoReportado = false; // Reseta a flag (pronto para disparar de novo)
      }
    }
  }
  // --- MODO: MANUAL (Controlado pelo Servidor) ---
  else if (modoAlarme.equals("manual")) {
    digitalWrite(LASER_PIN, HIGH); // Liga o laser
    ldrValue = analogRead(LDR_PIN); // Lê o sensor de luz

    // Passo 1: Apenas notificar o servidor se o feixe for interrompido
    if (ldrValue < LDR_THRESHOLD) { // Se o feixe foi cortado...
        if (!disparoReportado) { // E se for a primeira vez...
            Serial.println("-> MODO MANUAL: Feixe interrompido! Notificando o servidor.");
            enviar_estado_disparo("manual"); // Avisa o servidor
            disparoReportado = true; // Marca que "já avisamos"
        }
    } else { // Se o feixe voltou...
        if (disparoReportado) {
            Serial.println("-> MODO MANUAL: Feixe restaurado.");
            disparoReportado = false; // Reseta a flag
        }
    }

    // Passo 2: Verificar se o servidor mandou tocar o buzzer
    // Se o buzzer não estiver tocando E o servidor mandar tocar...
    if (!buzzerRemotoAtivo && verificar_buzzer_remoto()) {
        Serial.println("-> MODO MANUAL: Comando recebido! Ativando buzzer por 3s e resetando no servidor.");
        buzzerRemotoAtivo = true; // Marca que o buzzer está tocando
        buzzerRemotoStartTime = millis(); // Marca "quando" começou
        digitalWrite(BUZZER_PIN, LOW); // LIGA o buzzer
        resetar_buzzer_remoto(); // Avisa o servidor: "Ok, já estou tocando!"
    }
    
    // Passo 3: Gerenciar o temporizador do buzzer
    // Se o buzzer estiver tocando E já se passaram 3 segundos (3000ms)...
    if (buzzerRemotoAtivo && (millis() - buzzerRemotoStartTime > 3000)) {
        Serial.println("-> MODO MANUAL: Temporizador do buzzer terminou. Desligando.");
        buzzerRemotoAtivo = false; // Marca que o buzzer parou
        digitalWrite(BUZZER_PIN, HIGH); // DESLIGA o buzzer
    }
  }  
  // --- MODO: DESLIGADO ---
  else if (modoAlarme.equals("desligado")) {
    digitalWrite(LASER_PIN, LOW);   // Desliga o laser
    digitalWrite(BUZZER_PIN, HIGH); // Desliga o buzzer
    disparoReportado = false;       // Reseta as flags
    buzzerRemotoAtivo = false;
  }  
  // --- MODO: INDEFINIDO (ou qualquer outro) ---
  else { // Se o modo for "indefinido" ou algo deu errado
    digitalWrite(LASER_PIN, LOW);   // Desliga tudo por segurança
    digitalWrite(BUZZER_PIN, HIGH); 
  }

  // --- 5. MENSAGENS DE STATUS (Depuração) ---
  // Imprime o estado atual dos sensores no Monitor Serial
  Serial.print("  [Estado Atual] LDR: " + String(ldrValue));
  Serial.print(" | Laser: " + String(digitalRead(LASER_PIN) ? "LIGADO" : "DESLIGADO"));
  Serial.println(" | Buzzer: " + String(digitalRead(BUZZER_PIN) == LOW ? "TOCANDO" : "SILENCIO"));
  
  // --- 6. PAUSA ---
  delay(200); // Espera 0.2 segundos antes de começar o loop todo de novo.
}
