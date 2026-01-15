document.addEventListener('DOMContentLoaded', () => {
    const statusSpan = document.getElementById('status');
    const startButton = document.getElementById('startButton');
    const stopButton = document.getElementById('stopButton');
    const crashChartCanvas = document.getElementById('crashChart');

    let ws;
    let crashChart;

    function connectWebSocket() {
        // Determine WebSocket URL based on current host
        const wsProtocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        ws = new WebSocket(`${wsProtocol}//${window.location.host}/ws`);

        ws.onopen = () => {
            console.log('WebSocket connected');
            statusSpan.textContent = 'Connected, awaiting commands.';
            startButton.disabled = false;
            stopButton.disabled = true;
        };

        ws.onmessage = (event) => {
            const message = JSON.parse(event.data);
            console.log('Message from ESP32:', message);

            if (message.type === 'status') {
                statusSpan.textContent = message.data;
                if (message.data.includes('Armed')) {
                    startButton.disabled = true;
                    stopButton.disabled = false;
                } else if (message.data.includes('Idle') || message.data.includes('Connected')) {
                    startButton.disabled = false;
                    stopButton.disabled = true;
                }
            } else if (message.type === 'crashData') {
                displayCrashData(message.data);
                startButton.disabled = false; // Re-enable after crash data is displayed
                stopButton.disabled = true;
            }
        };

        ws.onclose = () => {
            console.log('WebSocket disconnected, attempting to reconnect...');
            statusSpan.textContent = 'Disconnected, reconnecting...';
            startButton.disabled = true;
            stopButton.disabled = true;
            setTimeout(connectWebSocket, 3000); // Attempt to reconnect every 3 seconds
        };

        ws.onerror = (error) => {
            console.error('WebSocket Error:', error);
            statusSpan.textContent = 'Connection error.';
            ws.close();
        };
    }

    function sendCommand(command) {
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(command);
        } else {
            console.warn('WebSocket not open. Cannot send command:', command);
            statusSpan.textContent = 'Not connected. Please refresh.';
        }
    }

    startButton.addEventListener('click', () => sendCommand('START'));
    stopButton.addEventListener('click', () => sendCommand('STOP'));

    function displayCrashData(data) {
        const labels = Array.from({ length: data.ax.length }, (_, i) => i);

        if (crashChart) {
            crashChart.destroy(); // Destroy previous chart if exists
        }

        crashChart = new Chart(crashChartCanvas, {
            type: 'line',
            data: {
                labels: labels,
                datasets: [
                    {
                        label: 'Accel X (m/s²)',
                        data: data.ax,
                        borderColor: 'rgb(255, 99, 132)',
                        tension: 0.1,
                        fill: false
                    },
                    {
                        label: 'Accel Y (m/s²)',
                        data: data.ay,
                        borderColor: 'rgb(54, 162, 235)',
                        tension: 0.1,
                        fill: false
                    },
                    {
                        label: 'Accel Z (m/s²)',
                        data: data.az,
                        borderColor: 'rgb(75, 192, 192)',
                        tension: 0.1,
                        fill: false
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                scales: {
                    x: {
                        title: {
                            display: true,
                            text: 'Sample Number',
                            color: '#e0e0e0'
                        },
                        ticks: {
                            color: '#b0b0b0'
                        },
                        grid: {
                            color: '#444'
                        }
                    },
                    y: {
                        title: {
                            display: true,
                            text: 'Acceleration (m/s²)',
                            color: '#e0e0e0'
                        },
                        ticks: {
                            color: '#b0b0b0'
                        },
                        grid: {
                            color: '#444'
                        }
                    }
                },
                plugins: {
                    legend: {
                        labels: {
                            color: '#e0e0e0'
                        }
                    },
                    title: {
                        display: true,
                        text: 'Last Recorded Crash Data',
                        color: '#ffffff',
                        font: {
                            size: 18
                        }
                    }
                }
            }
        });
    }

    connectWebSocket(); // Initial WebSocket connection
});