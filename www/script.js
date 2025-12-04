// www/script.js
console.log("ConcurrentHTTP Server: JavaScript carregado com sucesso!");

window.onload = function() {
    const statusDiv = document.getElementById('js-status');
    const date = new Date();
    
    if (statusDiv) {
        statusDiv.innerHTML = " JavaScript a funcionar! Hora do cliente: " + date.toLocaleTimeString();
        statusDiv.style.backgroundColor = "#e8f5e9";
        statusDiv.style.color = "#2e7d32";
    }
};