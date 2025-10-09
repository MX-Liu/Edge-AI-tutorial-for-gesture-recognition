#include <ArduinoBLE.h>
#include <Arduino_LSM9DS1.h>
#include <cmath>

// --- BLE Service and Characteristic Definition ---
// Define custom UUIDs for our service and characteristic
// You can generate your own UUIDs using an online tool
#define CUSTOM_SERVICE_UUID "19B10000-E8F2-537E-4F6C-D104768A1214"
#define DATA_CHAR_UUID      "19B10001-E8F2-537E-4F6C-D104768A1214"

// Define the size of our data payload: 6 float values (4 bytes each)
constexpr int SENSOR_DATA_SIZE = 6 * sizeof(float); // 24 bytes

// Create the BLE service and characteristic objects
BLEService imuService(CUSTOM_SERVICE_UUID);
BLECharacteristic cleanedDataCharacteristic(DATA_CHAR_UUID, BLENotify | BLERead, SENSOR_DATA_SIZE);

// --- Data Buffers and State Variables ---
constexpr int acceleration_data_length = 200 * 3;
float acceleration_data[acceleration_data_length] = {};
int acceleration_data_index = 0;

constexpr int gyroscope_data_length = 200 * 3;
float gyroscope_data[gyroscope_data_length] = {};
int gyroscope_data_index = 0;

float current_velocity[3] = {0.0f, 0.0f, 0.0f};
float current_gravity[3] = {0.0f, 0.0f, 0.0f};
float current_gyroscope_drift[3] = {0.0f, 0.0f, 0.0f};


// --- Data Reading and Cleaning Functions (Unchanged) ---

/**
 * @brief Reads all available samples from the IMU into circular buffers.
 */
void ReadAccelerometerAndGyroscope(int* new_accelerometer_samples,
                                   int* new_gyroscope_samples) {
  *new_accelerometer_samples = 0;
  *new_gyroscope_samples = 0;
  
  while (IMU.accelerationAvailable()) {
    const int acceleration_index = (acceleration_data_index % acceleration_data_length);
    acceleration_data_index += 3;
    float* current_acceleration_data = &acceleration_data[acceleration_index];
    if (!IMU.readAcceleration(current_acceleration_data[0],
                              current_acceleration_data[1],
                              current_acceleration_data[2])) {
      return; // Exit on failure
    }
    *new_accelerometer_samples += 1;

    const int gyroscope_index = (gyroscope_data_index % gyroscope_data_length);
    gyroscope_data_index += 3;
    float* current_gyroscope_data = &gyroscope_data[gyroscope_index];
    if (!IMU.readGyroscope(current_gyroscope_data[0], current_gyroscope_data[1],
                           current_gyroscope_data[2])) {
      return; // Exit on failure
    }
    *new_gyroscope_samples += 1;
  }
}

/**
 * @brief Calculates the magnitude of a 3D vector.
 */
float VectorMagnitude(const float* vec) {
  const float x = vec[0];
  const float y = vec[1];
  const float z = vec[2];
  return sqrtf((x * x) + (y * y) + (z * z));
}

/**
 * @brief Estimates the direction of gravity by averaging recent accelerometer readings.
 */
void EstimateGravityDirection(float* gravity) {
  int samples_to_average = 100;
  if (samples_to_average > acceleration_data_index / 3) {
    samples_to_average = acceleration_data_index / 3;
  }
  if (samples_to_average < 1) return;

  const int start_index = ((acceleration_data_index + (acceleration_data_length - (3 * samples_to_average))) % acceleration_data_length);
  float x_total = 0.0f, y_total = 0.0f, z_total = 0.0f;
  for (int i = 0; i < samples_to_average; ++i) {
    const int index = ((start_index + (i * 3)) % acceleration_data_length);
    x_total += acceleration_data[index + 0];
    y_total += acceleration_data[index + 1];
    z_total += acceleration_data[index + 2];
  }
  gravity[0] = x_total / samples_to_average;
  gravity[1] = y_total / samples_to_average;
  gravity[2] = z_total / samples_to_average;
}

/**
 * @brief Estimates the gyroscope's drift/bias when the device is stationary.
 */
void EstimateGyroscopeDrift(float* drift) {
  if (VectorMagnitude(current_velocity) > 0.1f) return;

  int samples_to_average = 20;
  if (samples_to_average > gyroscope_data_index / 3) {
    samples_to_average = gyroscope_data_index / 3;
  }
  if (samples_to_average < 1) return;

  const int start_index = ((gyroscope_data_index + (gyroscope_data_length - (3 * samples_to_average))) % gyroscope_data_length);
  float x_total = 0.0f, y_total = 0.0f, z_total = 0.0f;
  for (int i = 0; i < samples_to_average; ++i) {
    const int index = ((start_index + (i * 3)) % gyroscope_data_length);
    x_total += gyroscope_data[index + 0];
    y_total += gyroscope_data[index + 1];
    z_total += gyroscope_data[index + 2];
  }
  drift[0] = x_total / samples_to_average;
  drift[1] = y_total / samples_to_average;
  drift[2] = z_total / samples_to_average;
}

/**
 * @brief Updates the velocity estimate by integrating linear acceleration.
 */
void UpdateVelocity(int new_samples, float* gravity) {
  const int start_index = ((acceleration_data_index + (acceleration_data_length - (3 * new_samples))) % acceleration_data_length);
  const float friction_fudge = 0.98f;
  for (int i = 0; i < new_samples; ++i) {
    const int index = ((start_index + (i * 3)) % acceleration_data_length);
    const float ax = acceleration_data[index + 0];
    const float ay = acceleration_data[index + 1];
    const float az = acceleration_data[index + 2];
    current_velocity[0] += (ax - gravity[0]);
    current_velocity[1] += (ay - gravity[1]);
    current_velocity[2] += (az - gravity[2]);
    current_velocity[0] *= friction_fudge;
    current_velocity[1] *= friction_fudge;
    current_velocity[2] *= friction_fudge;
  }
}

/**
 * @brief Sends the latest batch of cleaned sensor data via BLE notifications.
 */
void SendCleanedDataBLE(int new_samples) {
  const int accel_start_index = ((acceleration_data_index + (acceleration_data_length - (3 * new_samples))) % acceleration_data_length);
  const int gyro_start_index = ((gyroscope_data_index + (gyroscope_data_length - (3 * new_samples))) % gyroscope_data_length);

  for (int i = 0; i < new_samples; ++i) {
    const int accel_index = (accel_start_index + (i * 3)) % acceleration_data_length;
    const int gyro_index = (gyro_start_index + (i * 3)) % gyroscope_data_length;

    float cleaned_data[6];
    cleaned_data[0] = acceleration_data[accel_index + 0] - current_gravity[0]; // linear_ax
    cleaned_data[1] = acceleration_data[accel_index + 1] - current_gravity[1]; // linear_ay
    cleaned_data[2] = acceleration_data[accel_index + 2] - current_gravity[2]; // linear_az
    cleaned_data[3] = gyroscope_data[gyro_index + 0] - current_gyroscope_drift[0]; // clean_gx
    cleaned_data[4] = gyroscope_data[gyro_index + 1] - current_gyroscope_drift[1]; // clean_gy
    cleaned_data[5] = gyroscope_data[gyro_index + 2] - current_gyroscope_drift[2]; // clean_gz
    
    // Send the 24-byte data packet over BLE
    cleanedDataCharacteristic.writeValue((uint8_t*)cleaned_data, SENSOR_DATA_SIZE);
  }
}


/**
 * @brief Initializes hardware and BLE communication.
 */
void setup() {
  // Initialize IMU
  if (!IMU.begin()) {
    while (1); // Halt on failure
  }
  IMU.setContinuousMode();
  
  // Initialize BLE
  if (!BLE.begin()) {
    while (1); // Halt on failure
  }
  
  // Set the advertised device name
  BLE.setLocalName("IMUCleanDataLIU");
  // Set the UUID for the service we will advertise
  BLE.setAdvertisedService(imuService);
  
  // Add the characteristic to the service
  imuService.addCharacteristic(cleanedDataCharacteristic);
  
  // Add the service
  BLE.addService(imuService);
  
  // Start advertising
  BLE.advertise();
}

/**
 * @brief Main loop for data collection, cleaning, and BLE transmission.
 */
void loop() {
  // Wait for a BLE central device to connect
  BLEDevice central = BLE.central();
  
  if (central) {
    // Check if the central is connected
    while (central.connected()) {
      int accelerometer_samples_read = 0;
      int gyroscope_samples_read = 0;

      // Read all available new data from the IMU
      ReadAccelerometerAndGyroscope(&accelerometer_samples_read, &gyroscope_samples_read);
      
      if (accelerometer_samples_read > 0) {
        // Update calibration values
        EstimateGravityDirection(current_gravity);
        EstimateGyroscopeDrift(current_gyroscope_drift);
        UpdateVelocity(accelerometer_samples_read, current_gravity);

        // Send the cleaned version of the new data over BLE
        SendCleanedDataBLE(accelerometer_samples_read);
      }
    }
  }
}