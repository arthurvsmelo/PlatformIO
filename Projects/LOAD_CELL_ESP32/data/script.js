// VARIAVEIS
var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
window.addEventListener('load', onLoad);

// retorna uma string com a data e a hora, para servir de nome do arquivo de amostras 
function getDate(){
    var date = new Date;
    const options = {
    month: 'numeric',
    day: 'numeric',
    };
    console.log(date.toLocaleDateString("pt-Br", options).replace("/", "-"));
    return date.toLocaleDateString("pt-Br", options).replace("/", "-");
}

function getTime(){
    var date = new Date;
    const options = {
        hour: "numeric",
        minute: "numeric"
        };
        console.log(date.toLocaleTimeString("pt-Br", options));
        return date.toLocaleTimeString("pt-Br", options);
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

var empuxo = [22, 24, 25, 30, 25, 28, 32, 25, 26, 27];
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

drawChart();


function onLoad(event) {
    initWebSocket();
    initButton();
}
function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    // chama essas funções de callback quando ocorrer esses eventos
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}

function onMessage(event) {
    console.log(event.data);
    var msg = JSON.parse(event.data);
    console.log(msg);
    if(msg.type == "sample"){
        document.getElementById("last_reading").innerHTML = msg.data.reading;
    }
    else if(msg.type == "sd_status"){
        document.getElementById("sd_info").innerHTML = msg.data;
    }
    else if(msg.type == "sample_running"){
        document.getElementById("status_info").innerHTML = msg.data;
    }    
}

function initButton() {
    // botão de iniciar amostragem
    document.getElementById("sample_button").addEventListener("click", function(event){
        event.preventDefault();
        if(confirm("Deseja realmente iniciar?")){
            let date = getDate();
            let time = getTime();
            websocket.send(JSON.stringify({"type":"sample_begin", "data":{"date":date, "time":time}}));
            console.log("ws send: ", date);
            console.log("ws send: ", time);
            document.getElementById("sample_button").disabled = true;
            setTimeout(function(){
                document.getElementById("sample_button").disabled = false;},5000);
        }
    });
}
setInterval(updateDateTime, 1000);
updateDateTime();