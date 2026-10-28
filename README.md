# Edge AI tutorial for gesture recognition (from sensor data collection to deployment)

An end‑to‑end workflow to collect IMU data from an Arduino Nano 33 BLE Sense, train a gesture recognition model, and deploy it back to the board using TensorFlow Lite for Microcontrollers.

Project structure
- `01_DataCollection/` — Arduino firmware to stream IMU over BLE + a Chrome Web Bluetooth app to visualize/record CSVs
- `02_Model_Training/` — Jupyter notebook to train, evaluate, convert to TFLite, and export a C array
- `03_Interence_on_MCU/` — Arduino inference sketch (runs the trained model on the MCU)

Refer to the README inside each folder for details.

---

## Requirements

Hardware
- Arduino Nano 33 BLE Sense (with onboard IMU)
- USB cable

Software
- Arduino IDE 2.x
- Google Chrome (latest)
- Python 3.10/3.11 (if training locally; otherwise use Google Colab)

Arduino IDE packages
- Boards: Arduino Mbed OS Nano Boards
- Libraries:
  - Data collection: ArduinoBLE, Arduino_LSM9DS1
  - Inference: Arduino_LSM9DS1, Arduino_TensorFlowLite (provided zip)

---

## Quick start

1) Collect data (BLE → Chrome)
- In Arduino IDE, open `01_DataCollection/datacollection_arduino_Nano33/datacollection.ino`.
- Install libraries ArduinoBLE + Arduino_LSM9DS1, select board "Arduino Nano 33 BLE Sense", upload.
- Open the web app:
  - Option A: double‑click `01_DataCollection/Webapp/index.html` to open it directly in Chrome.
  - Option B (fallback): serve the folder: `python3 -m http.server 8000` and open `http://localhost:8000`.
- Click "Connect to Device" and select `IMUCleanData`.
- Enter a label (e.g., `circle`), Start/Stop Recording, then Save to download CSV.

CSV format
- Header: `timestamp,label,ax,ay,az,gx,gy,gz`
- Units: accel in g, gyro in °/s

2) Train the model
- Put your CSVs under `02_Model_Training/data/`.
- Open `02_Model_Training/IMU_Gesture_Recognition_tutorial.ipynb` in Colab or run locally (see README in that folder for environment setup).
- Run all cells to train and evaluate. The notebook converts to TFLite and shows how to export a C array file `gesture_recognition_model_data.cpp`.
- Note: You can also access the script via this link: https://drive.google.com/file/d/1zr4YwVgvs94lRIoltW_OpwMIRW8i2aS7/view?usp=sharing

3) Deploy on the MCU
- Install `03_Interence_on_MCU/Arduino_TensorFlowLite.zip` via Arduino IDE → Sketch → Include Library → Add .ZIP Library…
- Copy the generated `gesture_recognition_model_data.cpp` from `02_Model_Training/` into `03_Interence_on_MCU/gesture_recognition_inference/` (replace the existing file).
- Open `03_Interence_on_MCU/gesture_recognition_inference/gesture_recognition.ino`.
- Ensure constants and preprocessing params match your notebook:
  - kSequenceLength (e.g., 50), kNumFeatures (6), kNumClasses and label order
  - SCALER_MEAN[] and SCALER_STD[] values
- Select the Nano 33 BLE Sense board/port and upload.
- Open Serial Monitor (9600 baud) to see predictions.

---

## BLE service and payload (data collection)
- Device name: `IMUCleanData`
- Service UUID: `19B10000-E8F2-537E-4F6C-D104768A1214`
- Characteristic UUID: `19B10001-E8F2-537E-4F6C-D104768A1214`
- Packet: 24 bytes, six little‑endian float32 values in order:
  1) linear_ax, 2) linear_ay, 3) linear_az, 4) clean_gx, 5) clean_gy, 6) clean_gz

---

## Troubleshooting (summary)
Data collection
- If Chrome blocks Web Bluetooth on `file://`, serve via `http://localhost`.
- Device not listed: ensure sketch uploaded, board is BLE Sense, and it’s not connected elsewhere.

Training
- TensorFlow install errors on macOS: try `tensorflow==2.15.*` or `tensorflow-macos` + `tensorflow-metal` on Apple Silicon.
- Unbalanced classes lead to poor accuracy—record more data per class.

Inference
- Missing TensorFlowLite headers: install the zip via Add .ZIP Library.
- AllocateTensors failed: increase tensor arena or reduce model size.
- Wrong predictions: update SCALER_MEAN/STD and ensure label order matches training.

---

## References
- `01_DataCollection/README.md` — BLE firmware + Web app usage
- `02_Model_Training/README.md` — environment, notebook, export steps
- `03_Interence_on_MCU/README.md` — library install, build/upload, ops resolver notes
