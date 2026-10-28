const CUSTOM_SERVICE_UUID = "19b10000-e8f2-537e-4f6c-d104768a1214";
const DATA_CHAR_UUID = "19b10001-e8f2-537e-4f6c-d104768a1214";
const MAX_DATA_POINTS = 120;
const CHART_REFRESH_MS = 50;

const elements = {
    connectBtn: document.getElementById("connectBtn"),
    disconnectBtn: document.getElementById("disconnectBtn"),
    status: document.getElementById("status"),
    statusTitle: document.getElementById("statusTitle"),
    statusDetail: document.getElementById("statusDetail"),
    browserNotice: document.getElementById("browserNotice"),
    labelInput: document.getElementById("labelInput"),
    labelHelp: document.getElementById("labelHelp"),
    recordBtn: document.getElementById("recordBtn"),
    saveBtn: document.getElementById("saveBtn"),
    sampleCount: document.getElementById("sampleCount"),
    recordingTime: document.getElementById("recordingTime"),
    clearChartBtn: document.getElementById("clearChartBtn"),
    chartFallback: document.getElementById("chartFallback"),
    toast: document.getElementById("toast"),
    values: {
        ax: document.getElementById("ax"), ay: document.getElementById("ay"), az: document.getElementById("az"),
        gx: document.getElementById("gx"), gy: document.getElementById("gy"), gz: document.getElementById("gz")
    }
};

let bleDevice = null;
let dataCharacteristic = null;
let isConnecting = false;
let isRecording = false;
let recordedData = [];
let recordingStartedAt = 0;
let recordingTimer = null;
let imuChart = null;
let latestSensorData = null;
let uiFrameRequested = false;
let lastChartUpdate = 0;
let streamStartedAt = 0;
let toastTimer = null;

elements.connectBtn.addEventListener("click", connectToDevice);
elements.disconnectBtn.addEventListener("click", disconnectDevice);
elements.recordBtn.addEventListener("click", toggleRecording);
elements.saveBtn.addEventListener("click", saveData);
elements.clearChartBtn.addEventListener("click", clearChart);
elements.labelInput.addEventListener("input", clearLabelError);
window.addEventListener("beforeunload", disconnectDevice);

initialize();

function initialize() {
    initChart();
    if (!("bluetooth" in navigator)) {
        elements.browserNotice.hidden = false;
        elements.connectBtn.disabled = true;
        setStatus("error", "Bluetooth unavailable", "Use desktop Chrome or Edge over localhost.");
    }
}

async function connectToDevice() {
    if (isConnecting || bleDevice?.gatt?.connected) return;
    isConnecting = true;
    setConnectionControls();

    try {
        setStatus("working", "Choose your device", "Select IMUCleanData in the browser prompt.");
        bleDevice = await navigator.bluetooth.requestDevice({ filters: [{ services: [CUSTOM_SERVICE_UUID] }] });
        bleDevice.addEventListener("gattserverdisconnected", onDisconnected);

        setStatus("working", "Connecting…", "Opening a secure Bluetooth connection.");
        const server = await bleDevice.gatt.connect();
        const service = await server.getPrimaryService(CUSTOM_SERVICE_UUID);
        dataCharacteristic = await service.getCharacteristic(DATA_CHAR_UUID);
        await dataCharacteristic.startNotifications();
        dataCharacteristic.addEventListener("characteristicvaluechanged", handleNotification);

        streamStartedAt = performance.now();
        document.body.classList.add("connected");
        setStatus("connected", bleDevice.name || "Device connected", "Receiving live IMU measurements.");
        showToast(`${bleDevice.name || "Arduino"} is connected.`);
    } catch (error) {
        const failedDevice = bleDevice;
        cleanUpConnection();
        if (failedDevice?.gatt?.connected) failedDevice.gatt.disconnect();
        if (error.name === "NotFoundError") {
            setStatus("disconnected", "Not connected", "Device selection was cancelled.");
        } else {
            console.error("Bluetooth connection failed:", error);
            setStatus("error", "Connection failed", friendlyBluetoothError(error));
            showToast(friendlyBluetoothError(error), true);
        }
    } finally {
        isConnecting = false;
        setConnectionControls();
    }
}

function disconnectDevice() {
    if (bleDevice?.gatt?.connected) bleDevice.gatt.disconnect();
}

function onDisconnected() {
    const deviceName = bleDevice?.name || "Device";
    if (isRecording) stopRecording();
    cleanUpConnection();
    setStatus("disconnected", "Not connected", "Reconnect when your board is ready.");
    setConnectionControls();
    showToast(`${deviceName} disconnected.`);
}

function cleanUpConnection() {
    if (dataCharacteristic) dataCharacteristic.removeEventListener("characteristicvaluechanged", handleNotification);
    if (bleDevice) bleDevice.removeEventListener("gattserverdisconnected", onDisconnected);
    dataCharacteristic = null;
    bleDevice = null;
    document.body.classList.remove("connected");
}

function setConnectionControls() {
    const connected = Boolean(bleDevice?.gatt?.connected);
    elements.connectBtn.disabled = isConnecting || connected || !("bluetooth" in navigator);
    elements.connectBtn.querySelector("span:last-child").textContent = isConnecting ? "Connecting…" : "Connect device";
    elements.disconnectBtn.disabled = !connected;
    elements.recordBtn.disabled = !connected;
}

function handleNotification(event) {
    const value = event.target.value;
    if (!value || value.byteLength !== 24) return;
    const sensorData = {
        ax: value.getFloat32(0, true), ay: value.getFloat32(4, true), az: value.getFloat32(8, true),
        gx: value.getFloat32(12, true), gy: value.getFloat32(16, true), gz: value.getFloat32(20, true)
    };
    if (!Object.values(sensorData).every(Number.isFinite)) return;

    latestSensorData = sensorData;
    scheduleUIUpdate();
    if (isRecording) {
        recordedData.push([
            Date.now(), elements.labelInput.value.trim(), sensorData.ax, sensorData.ay, sensorData.az,
            sensorData.gx, sensorData.gy, sensorData.gz
        ]);
        elements.sampleCount.textContent = recordedData.length.toLocaleString();
    }
}

function scheduleUIUpdate() {
    if (uiFrameRequested) return;
    uiFrameRequested = true;
    requestAnimationFrame((time) => {
        uiFrameRequested = false;
        if (!latestSensorData) return;
        updateValues(latestSensorData);
        if (time - lastChartUpdate >= CHART_REFRESH_MS) {
            updateChart(latestSensorData, time);
            lastChartUpdate = time;
        }
    });
}

function updateValues(data) {
    Object.entries(data).forEach(([key, value]) => { elements.values[key].textContent = formatValue(value); });
}

function formatValue(value) {
    const rounded = Math.abs(value) < 0.0005 ? 0 : value;
    return rounded.toFixed(3);
}

function toggleRecording() {
    if (isRecording) stopRecording();
    else startRecording();
}

function startRecording() {
    const label = elements.labelInput.value.trim();
    if (!label) {
        elements.labelInput.classList.add("invalid");
        elements.labelInput.setAttribute("aria-invalid", "true");
        elements.labelHelp.textContent = "Add a gesture label before recording.";
        elements.labelInput.focus();
        showToast("Add a gesture label first.", true);
        return;
    }

    recordedData = [];
    isRecording = true;
    recordingStartedAt = performance.now();
    elements.sampleCount.textContent = "0";
    elements.recordingTime.textContent = "00:00.0";
    elements.labelInput.disabled = true;
    elements.saveBtn.disabled = true;
    elements.recordBtn.classList.add("recording");
    elements.recordBtn.querySelector("span:last-child").textContent = "Stop recording";
    setStatus("connected", "Recording gesture", `Capturing “${label}” samples now.`);
    updateRecordingTime();
    recordingTimer = window.setInterval(updateRecordingTime, 100);
}

function stopRecording() {
    if (!isRecording) return;
    isRecording = false;
    window.clearInterval(recordingTimer);
    recordingTimer = null;
    updateRecordingTime();
    elements.labelInput.disabled = false;
    elements.recordBtn.classList.remove("recording");
    elements.recordBtn.querySelector("span:last-child").textContent = "Start recording";
    elements.saveBtn.disabled = recordedData.length === 0;

    if (bleDevice?.gatt?.connected) {
        setStatus("connected", bleDevice.name || "Device connected", `${recordedData.length.toLocaleString()} samples ready to save.`);
    }
    if (recordedData.length > 0) showToast(`Captured ${recordedData.length.toLocaleString()} samples.`);
}

function updateRecordingTime() {
    if (!recordingStartedAt) return;
    const elapsed = Math.max(0, performance.now() - recordingStartedAt);
    const minutes = Math.floor(elapsed / 60000);
    const seconds = Math.floor((elapsed % 60000) / 1000);
    const tenths = Math.floor((elapsed % 1000) / 100);
    elements.recordingTime.textContent = `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}.${tenths}`;
}

function clearLabelError() {
    elements.labelInput.classList.remove("invalid");
    elements.labelInput.removeAttribute("aria-invalid");
    elements.labelHelp.textContent = "Use one consistent label for each gesture type.";
}

function saveData() {
    if (recordedData.length === 0) {
        showToast("Record some samples before saving.", true);
        return;
    }
    const header = "timestamp,label,ax,ay,az,gx,gy,gz\n";
    const csvContent = header + recordedData.map((row) => row.map(escapeCsvCell).join(",")).join("\n");
    const blob = new Blob([csvContent], { type: "text/csv;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    const label = sanitizeFilename(recordedData[0][1] || "data");
    const timestamp = new Date().toISOString().slice(0, 19).replace("T", "_").replaceAll(":", "-");

    link.href = url;
    link.download = `imu-${label}-${timestamp}.csv`;
    document.body.appendChild(link);
    link.click();
    link.remove();
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
    showToast(`Saved ${recordedData.length.toLocaleString()} samples as CSV.`);
}

function escapeCsvCell(value) {
    const text = String(value);
    return /[",\n]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text;
}

function sanitizeFilename(value) {
    const cleaned = value.toLowerCase().replace(/[^a-z0-9_-]+/g, "-").replace(/^-+|-+$/g, "");
    return cleaned || "data";
}

function initChart() {
    if (typeof Chart === "undefined") {
        elements.chartFallback.hidden = false;
        elements.clearChartBtn.disabled = true;
        return;
    }
    const context = document.getElementById("imuChart").getContext("2d");
    imuChart = new Chart(context, {
        type: "line",
        data: {
            labels: [],
            datasets: [
                chartDataset("Accel X", "#65aaff", "yAccel"),
                chartDataset("Accel Y", "#73f0c5", "yAccel"),
                chartDataset("Accel Z", "#ffc66d", "yAccel"),
                chartDataset("Gyro X", "#a88cff", "yGyro", [5, 4]),
                chartDataset("Gyro Y", "#ff83bd", "yGyro", [5, 4]),
                chartDataset("Gyro Z", "#ff8d73", "yGyro", [5, 4])
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            normalized: true,
            interaction: { intersect: false, mode: "index" },
            plugins: {
                legend: {
                    position: "top",
                    align: "start",
                    labels: { color: "#8ba0a9", boxWidth: 18, boxHeight: 2, padding: 16, usePointStyle: false, font: { size: 10, weight: 600 } }
                },
                tooltip: {
                    backgroundColor: "#102231", borderColor: "rgba(151,181,188,.2)", borderWidth: 1,
                    titleColor: "#8ba0a9", bodyColor: "#eaf3f4", padding: 10
                }
            },
            scales: {
                x: {
                    grid: { color: "rgba(151,181,188,.06)" }, border: { display: false },
                    ticks: { color: "#526872", maxTicksLimit: 6, font: { size: 9 } },
                    title: { display: true, text: "seconds", color: "#526872", font: { size: 9 } }
                },
                yAccel: {
                    position: "left", grid: { color: "rgba(151,181,188,.08)" }, border: { display: false },
                    ticks: { color: "#526872", maxTicksLimit: 6, font: { size: 9 } },
                    title: { display: true, text: "acceleration (g)", color: "#526872", font: { size: 9 } }
                },
                yGyro: {
                    position: "right", grid: { drawOnChartArea: false }, border: { display: false },
                    ticks: { color: "#526872", maxTicksLimit: 6, font: { size: 9 } },
                    title: { display: true, text: "rotation (°/s)", color: "#526872", font: { size: 9 } }
                }
            }
        }
    });
}

function chartDataset(label, color, yAxisID, borderDash = []) {
    return {
        label, data: [], yAxisID, borderColor: color, backgroundColor: color, borderWidth: 1.6,
        borderDash, pointRadius: 0, pointHoverRadius: 3, tension: 0.18
    };
}

function updateChart(data, time) {
    if (!imuChart) return;
    if (imuChart.data.labels.length >= MAX_DATA_POINTS) {
        imuChart.data.labels.shift();
        imuChart.data.datasets.forEach((dataset) => dataset.data.shift());
    }
    const seconds = ((time - streamStartedAt) / 1000).toFixed(1);
    imuChart.data.labels.push(seconds);
    [data.ax, data.ay, data.az, data.gx, data.gy, data.gz].forEach((value, index) => {
        imuChart.data.datasets[index].data.push(value);
    });
    imuChart.update("none");
}

function clearChart() {
    if (!imuChart) return;
    imuChart.data.labels.length = 0;
    imuChart.data.datasets.forEach((dataset) => { dataset.data.length = 0; });
    streamStartedAt = performance.now();
    imuChart.update("none");
}

function setStatus(type, title, detail) {
    elements.status.className = `status status-${type}`;
    elements.statusTitle.textContent = title;
    elements.statusDetail.textContent = detail;
}

function friendlyBluetoothError(error) {
    if (error.name === "SecurityError") return "Bluetooth needs HTTPS or http://localhost.";
    if (error.name === "NetworkError") return "The board stopped responding. Reset it and try again.";
    if (error.name === "NotSupportedError") return "This device does not expose the expected IMU service.";
    return "Check that the board is powered, nearby, and not connected elsewhere.";
}

function showToast(message, isError = false) {
    window.clearTimeout(toastTimer);
    elements.toast.textContent = message;
    elements.toast.className = `toast show${isError ? " error" : ""}`;
    toastTimer = window.setTimeout(() => { elements.toast.className = "toast"; }, 3200);
}
