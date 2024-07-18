// VARIAVEIS
var gateway = `ws://${window.location.hostname}/ws`;
var websocket;


// retorna uma string com a data e a hora, para servir de nome do arquivo de amostras 
function sendDateTime(){
    date_time = new Date();
    return date_time.toLocaleString().replace(/\s+|[,\/]/g, "-");
}
// Atualiza a data e a hora a cada segundo, para ser exibida na página html
function updateDateTime() {
    const now = new Date();
    const datetimeDiv = document.getElementById('datetime');
    const options = { 
        weekday: 'short', year: 'numeric', month: 'long', 
        day: 'numeric', hour: '2-digit', minute: '2-digit', second: '2-digit' 
    };
    datetimeDiv.textContent = now.toLocaleDateString('pt-BR', options);
}
// recebe os dados do ESP e armazena para construir o gráfico
function dataStorage(){

}
var empuxo = [12, 19, 25, 30, 18, 20, 32, 25, 18, 27];
var tempo = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
function drawChart(empuxo, tempo){
    // Configuração do gráfico usando Chart.js
    var ctx = document.getElementById('myChart').getContext('2d');
    var myChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10],
            datasets: [{
                label: 'Empuxo (N)',
                data: [12, 19, 25, 30, 18, 20, 32, 25, 18, 27],
                backgroundColor: 'rgba(153, 165, 195, 0.6)',
                borderColor: 'rgba(15, 15, 15, 0.85)',
                borderWidth: 2,
                borderJoinStyle: 'round',
                fill: 'origin',
                tension: 0.2
            }]
        },
        options: {
            scales: {
                yAxes: [{
                    ticks: {
                        beginAtZero: true
                    }
                }],
            },
            plugins: {
                legend: {
                    labels: {
                        font: {
                            size: 20
                        }
                    }
                }
            }
        }
    });
}

// envia mensagens de log ou msg de erro
function logMessage(message) {
    const logDiv = document.getElementById('log');
    const newMessage = document.createElement('div');
    newMessage.textContent = message;
    logDiv.appendChild(newMessage);
    logDiv.scrollTop = logDiv.scrollHeight; // Scroll to the bottom of the log
}

// Função para atualizar o conteúdo dos cards
function updateInfo(infoId, newContent) {
    const card = document.getElementById(infoId);
    if (card) {
        card.textContent = newContent;
        console.log(`Atualizado ${infoId} com novo conteúdo: ${newContent}`);
    } else {
        console.log(`Erro: ${infoId} não encontrado`);
    }
}

drawChart();


function onLoad(event) {
    initWebSocket();
}
function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
    websocket.send("states");
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}

function onMessage(event) {
    var myObj = JSON.parse(event.data);
    console.log(myObj);
    for (i in myObj.gpios){
        var output = myObj.gpios[i].output;
        var state = myObj.gpios[i].state;
        console.log(output);
        console.log(state);
        if (state == "1"){
            document.getElementById(output).checked = true;
            document.getElementById(output+"s").innerHTML = "ON";
        }
        else{
        document.getElementById(output).checked = false;
        document.getElementById(output+"s").innerHTML = "OFF";
        }
    }
    console.log(event.data);
}
// Send Requests to Control GPIOs
function toggleCheckbox (element) {
    console.log(element.id);
    websocket.send(element.id);
    if (element.checked){
        document.getElementById(element.id+"s").innerHTML = "ON";
    }
    else {
        document.getElementById(element.id+"s").innerHTML = "OFF";
    }
}
// Function to get and update GPIO states on the webpage when it loads for the first time
function getStates(){
    websocket.send("states");
}
window.addEventListener('load', onLoad);
setInterval(updateDateTime, 1000);
updateDateTime(); // Chama a função imediatamente para definir a data e a hora ao carregar a página
// Exemplo de uso inicial para testar a função updateCard
updateInfo('status_info', 'Stand-by');
updateInfo('sd_info', 'Ok');