//biblioteca necessária para uso do protocolo MQTT
#include <PubSubClient.h>
//biblioteca que contém bibliotecas de manipulação de strings
#include <string.h>
//biblioteca que contém recursos para conexão WIFI
//Quando esta biblioteca é utilizada, o ADC1 deixa de funcionar
#include <WiFi.h>


#define entradaHidrometro 34
#define pinoBomba 4
//valor de umidade mínimo para que o sistema ligue o motor ou não
#define umidadeDeDecisao 50
//Tamanho máximo em número de caracteres para uma mensagem MQTT
#define tamanhoDoVetorMensagem 200


//Broker
const char* urlMqtt = "test.mosquitto.org";
const char* nomeDaRedeDaCasa = "Sandy 2.4";
const char* senhaDaRedeDaCasa = "17012005";

//variável responsável por verificar se o sistema está em modo automático, ou seja, se o próprio ESP avalia a umidade e Automatico
//a decisão de ligar ou não a bomba
bool modoAutomatico = true;
//flag que indica se chegou comando pedindo status via mqtt ou não
bool getStatus = false;
//variável que armazena o valor lido pela porta analógica, valor entre 0 e 4095(resolução 12 bits)
int valorAnalogicoLido = 0;
//variável que armazenará a umidade do solo lida
double umidadeDoSolo = 0.00;
//variável que indica se a bomba está ligada ou não
bool statusBomba = false;
//valor de umidade de água, (VALOR INTEIRO)
int umidadeInteira = 0;

//variáveis responsáveis pela manipulação de dados MQTT
WiFiClient espClient;
PubSubClient client(espClient);

//vetor de caracteres(Strings) que armazenarão a mensagem que chega via mqtt, o status a ser enviado pelo ESP via mqtt e o tópico MQTT
char vetorMensagem[tamanhoDoVetorMensagem];
char vetorStatus[tamanhoDoVetorMensagem];
char vetorTopico[tamanhoDoVetorMensagem];


//função que recebe o valor analógico lido, e faz o cálculo deste valor baseado na equação da reta, dados os seguintes pontos:
//Para valor analógico igual a 4095, entendemos que a umidade do solo é 0%
//Para valor analógico igual a 2900(obtido por calibração do sensor de umidade), entendemos que a umidade do solo é 100%

double calcularUmidadeDoSolo(int valorAnalogico){
	return (valorAnalogico*(-0.0836820083682008)) + 342.677824267782;
}

//função que preenche todos os caracteres do vetor mensagem com '\0'
void limparVetorMensagem(){
	int i=0;

	for(i=0;i<tamanhoDoVetorMensagem;i++){
		vetorMensagem[i] = '\0';
	}
}

//função que preenche todos os caracteres do vetor status com '\0'
void limparVetorStatus(){
	int i=0;

	for(i=0;i<tamanhoDoVetorMensagem;i++){
		vetorStatus[i] = '\0';
	}
}


//função que preenche todos os caracteres do vetor topico com '\0'
void limparVetorTopico(){
	int i=0;

	for(i=0;i<tamanhoDoVetorMensagem;i++){
		vetorTopico[i] = '\0';
	}
}

//esta função é executada toda vez que uma nova mensagem chega no broker, no tópico em que o esp32 está inscrito
void callback(char* topic, byte* message, unsigned int length) { //funçao executada quando chega mensagem no tópico
	int i=0;

	//limpando os vetores mensagem e status para serem usados em uma nova leitura
	limparVetorMensagem();
	limparVetorTopico();


	//transferindo a mensagem recém-chegada para o vetorMensagem
	for (i = 0; i < length; i++) {
		vetorMensagem[i] = (char) message[i];
	}

	//verificando se o tópico em que a mensagem foi postada, é o tópico em que o esp está inscrito
	if(String(topic) == "appDanielMqttUmidade") {
		//se sim, vamos analisar qual é a mensagem que chegou, e tomar a ação de acordo com a mensagem correta

		//se for modoAuto, colocamos o esp em modo automático
		if(strcmp(vetorMensagem,"modoAuto") == 0){
			modoAutomatico = true;
		}
		//se for modoManual, colocamos o esp em modo manual
		else if(strcmp(vetorMensagem,"modoManual") == 0){
			modoAutomatico = false;
		}

		//se for getStatus, setamos a flag getStatus e então o esp postará o status no tópico o mais rápido possível
		//não dá para fazer isso dentro da função callback
		else if(strcmp(vetorMensagem,"getStatus") == 0){
			getStatus = true;
		}

		//se a mensagem for motorOn, ligamos o motor
		else if(strcmp(vetorMensagem,"motorOn") == 0){
			digitalWrite(pinoBomba,HIGH);
			statusBomba = true;
		}
		//se a mensagem for motorOff, desligamos o motor
		else if(strcmp(vetorMensagem,"motorOff") == 0){
			digitalWrite(pinoBomba,LOW);
			statusBomba = false;
		}
	}
}

//função a ser executada caso haja perda de conexão com o broker mqtt
void reconnect() {
	//loop de conexão em que só será encerrado quando a conexão com o broker for reestabelecida
	while (!client.connected()) {
		Serial.print("Tentando se conectar ao broker MQTT...");
		// Attempt to connect
		if(client.connect("Esp32Client")) {
			Serial.println("connected");
			client.subscribe("appDanielMqttUmidade");
		}
		else {
			Serial.print("failed, rc=");
			Serial.print(client.state());
			Serial.println(" try again in 5 seconds");
			delay(5000);
		}
	}
}

//função de configuração do mqtt
void configurarMqtt(){
	//primeiramente, nos conectamos a rede da casa
	WiFi.begin(nomeDaRedeDaCasa,senhaDaRedeDaCasa);
	//loop de espera, pra que a conexão WiFi seja estabelecida com sucesso
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}

	//configuramos a conexão com o broker mqtt, por meio da porta 1883
	client.setServer(urlMqtt, 1883);
	//configuramos o callback, que é a função a ser executada quando o esp detecta que uma nova mensagem chegou ao broker
	client.setCallback(callback);

}

//variáveis para auxílio na medição de tempo
//o esp realiza uma leitura analógica de umidade a cada 0.1 segundo
//o esp envia o status ao broker, no tópico a cada 7 segundos
long tempoAgora = 0;
long ultimoTempoLido = 0;
long ultimoTempoDeFeedback = 0;

//setup
void setup() {
	//inicializamos a interface serial
	Serial.begin(9600);
	//configuraamos o pino 34, de leitura do hidrõmetro, como entrada
	pinMode(entradaHidrometro, INPUT);
	//configuramos o pino 4, que é o pino em que o driver da bomba está conectado, como saída
	pinMode(pinoBomba,OUTPUT);
	//desligamos a bomba
	digitalWrite(pinoBomba,LOW);
	//configuramos a variável de status da bomba como estando desligada
	statusBomba = false;
	//chamamos a função de configuração do mqtt
	configurarMqtt();
}

//função responsável pelo envio da mensagem de status ao broker mqtt
void mandarStatus(){
	//limpamos o vetor de status
	limparVetorStatus();
	//neste ponto, o vetor de status está vazio, então, pegamos a última leitura de umidade ligada
	//convertemos esta leitura, que é um inteiro, para string, e então armazenamos esta string no vetorStatus
	itoa(umidadeInteira,vetorStatus,10);
	//neste ponto, o conteúdo no vetor status é a umidade. Para umidade igual a 25% o conteúdo no vetorStatus será
	//"25"

	//se a bomba estiver ligada, adicionamos ao final da string no vetorStatus, a string "%@ligado"
	//caso contrário, adicionamos a string "%@desligado"
	if(statusBomba == true){
		strcat(vetorStatus,"%@ligado");
	}
	else{
		strcat(vetorStatus,"%@desligado");
	}

	//neste ponto, se por exemplo a bomba estiver desligada, o conteúdo do vetorStatus será: "25%@desligado"

	//Se o sistema estiver em modo automático, adicionamos ao final a string "@modoAuto", e "@modoManual" caso contrário.

	if(modoAutomatico == true){
		strcat(vetorStatus,"@modoAuto");
	}
	else{
		strcat(vetorStatus,"@modoManual");
	}

	//neste ponto, se por exemplo a bomba estiver desligada, e o esp estiver em modo manual o conteúdo do vetorStatus será: "25%@desligado@modoManual"

	//enviamos a mensagem ao broker mqtt, no tópico appDanielMqttUmidade
	client.publish("appDanielMqttUmidade",vetorStatus);
	//colocamos a flag getStatus como false novamente. Garantindo que assim que o status seja enviado ao broker quando um novo ciclo de 7 segundos for completado
	//ou quando chegar uma mensagem mqtt no tópico appDanielMqttUmidade pedindo explicitamente que o status seja enviado pelo esp para o broker
	getStatus = false;


}


void loop() {
	//caso o esp perca a conexão com o broker, tentamos a reconexão
	if (!client.connected()) {
        reconnect();
    }
	//esta linha de código é necessária para que o esp possa receber o conteúdo postado no broker em tempo real
    client.loop();

	//medimos o tempo atual
	tempoAgora = millis();

	//se passaram-se 100 ms desde a última medição, entramos no if
	//isso é necessário para garantimos que a leitura analógica seja realizada a cada 100 ms
	if(tempoAgora - ultimoTempoLido > 100) {
		//atualizamos a variável ultimoTempoLido, com o tempo mais recente
		ultimoTempoLido = tempoAgora;


		//imprimimos a última mensagem que chegou via mqtt
		Serial.print("Ultima mensagem mqtt: ");
		Serial.println(vetorMensagem);
		//realizamos a leitura analógica de umidade, valor de 0 a 4095
		valorAnalogicoLido = analogRead(entradaHidrometro);
		//imprimimos o valor analógico lido
		Serial.print("Valor Analogico Lido: ");
		Serial.print(valorAnalogicoLido);

		//chamamos a função que converte este valor de 0 a 4095, em umidade percentual, de 0 a 100%
		//armazenamos este valor, que é decimal, na variável umidadeDoSolo
		umidadeDoSolo = calcularUmidadeDoSolo(valorAnalogicoLido);
		//a variável umidadeInteira fica com a parte inteira da umidadeDoSolo
		umidadeInteira = (int) umidadeDoSolo;

		//imprimimos a umidade do solo
		Serial.print("--Umidade do solo: ");
		Serial.print(umidadeDoSolo);

		Serial.print("%--Decisao: ");

		//imprimimos se o sistema está em modo automático ou manual
		Serial.print("Modo Automatico: ");
		Serial.print(modoAutomatico);

		//se o sistema estiver em modo automático, o próprio esp avaliará se a bomba deve ser desligada ou não
		if(modoAutomatico == true){
			//se a umidade do solo for maior que a umidadeDeDecisao, que no caso é 50%, a bomba é desligada
			if(umidadeDoSolo >= umidadeDeDecisao){
				digitalWrite(pinoBomba,LOW);
				statusBomba = false;
				Serial.println("DESLIGAMOS A BOMBA");
			}
			//senão, a bomba é ligada
			else{
				digitalWrite(pinoBomba,HIGH);
				statusBomba = true;
				Serial.println("LIGAMOS A BOMBA");
			}
		}


		//se a flag getStatus estiver acesa, significa que uma mensagem pedindo o status do sistema chegou via mqtt,
		//então, devemos enviar o status via mqtt
		if(getStatus == true){
			mandarStatus();
		}

		//se passaram-se 7 segundos desde o último envio de status, também devemos enviar o status via mqtt
		if((tempoAgora - ultimoTempoDeFeedback) > 7000){
			//repare que a variável ultimoTempoDeFeedback é atualizada apenas quando se passam os 7 segundos
			ultimoTempoDeFeedback = tempoAgora;
			mandarStatus();
		}
	}
}
