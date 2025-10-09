/*
 * IMU Gesture Recognition - Inference Sketch (2D CNN Version)
 *
 * This sketch is updated to run a 2D Convolutional Neural Network on
 * time-series data. It is compatible with TensorFlow Lite libraries that
 * only support 2D operations.
 *
 * The pipeline remains the same, but the model architecture and its setup are different.
 */

// --- Step 1: Include Libraries ---
#include <Arduino_LSM9DS1.h>
#include <TensorFlowLite.h>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"


// Include the model header file you just re-generated with the new 2D model.
#include "gesture_recognition_model_data.h"

// --- Step 2: Model and Data Configuration ---
constexpr int kSequenceLength = 50; // Height of our "image"
constexpr int kNumFeatures = 6;     // Width of our "image"
constexpr int kNumClasses = 4;      // circle, null, rightleft, updown

// --- Step 3: Define Gesture Labels (order must match notebook) ---
const char* GESTURE_LABELS[kNumClasses] = {
    "circle", "null", "rightleft", "updown"
};

// --- Step 4: Define Preprocessing Constants (Scaler Values) ---
// !!! CRITICAL: These values must come from your notebook after training the NEW model !!!
// They might change slightly with the new architecture. Re-run the scaler print cell.
const float SCALER_MEAN[kNumFeatures] = {
  -0.08942296f, 0.45281538f, 0.05263595f, 5.09340668f, -2.17296052f, 0.52844119f
};
const float SCALER_STD[kNumFeatures] = {
  0.64414173f, 1.15174937f, 1.13781798f, 110.28385925f, 142.06216431f, 137.49159241f
};

// --- Step 5: Set up TensorFlow Lite Globals ---
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* model_input = nullptr;
TfLiteTensor* model_output = nullptr;
constexpr int kTensorArenaSize = 128 * 1024; // This size should still be sufficient
alignas(16) uint8_t tensor_arena[kTensorArenaSize];

// --- Data Buffers and Cleaning Globals (No Changes) ---
constexpr int SENSOR_HISTORY_LENGTH = 100;
float sensor_history_buffer[SENSOR_HISTORY_LENGTH * kNumFeatures] = {0.0f};
int history_buffer_idx = 0;
float inference_buffer[kSequenceLength * kNumFeatures];
int inference_samples_collected = 0;
float current_gravity[3] = {0.0f, 0.0f, 0.0f};
float current_gyroscope_drift[3] = {0.0f, 0.0f, 0.0f};
float current_velocity[3] = {0.0f, 0.0f, 0.0f};

// --- Data Cleaning and Helper Functions (No Changes from previous sketch) ---
float VectorMagnitude(const float* vec) { return sqrtf((vec[0] * vec[0]) + (vec[1] * vec[1]) + (vec[2] * vec[2])); }
void EstimateGravityDirection() { /* ... same as before ... */ }
void UpdateVelocity(float ax, float ay, float az) { /* ... same as before ... */ }
void EstimateGyroscopeDrift() { /* ... same as before ... */ }


// --- Main Arduino `setup()` function ---
void setup() {
  // Start serial communication for printing output.
  Serial.begin(9600);
  // Add a small delay to ensure the Serial Monitor can connect in time.
  delay(1000); 
  Serial.println("--- Gesture Recognition Inference Sketch Starting ---");

  // Initialize the IMU sensor.
  if (!IMU.begin()) {
    Serial.println("FATAL: Failed to initialize IMU!");
    while (1); // Halt execution.
  }
  IMU.setContinuousMode();
  Serial.println("IMU Initialized successfully.");

  // --- TensorFlow Lite Setup ---
  // 1. Load the model from the C byte array.
  model = tflite::GetModel(g_gesture_recognition_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    MicroPrintf("FATAL: Model is an unsupported version. Expected %d, got %d", TFLITE_SCHEMA_VERSION, model->version());
    return;
  }
  Serial.println("Model loaded successfully.");

  // 2. Define the operations (layers) that our model uses.
  //    This list MUST match the model architecture exactly.
  //
  //    !!! --- THIS IS THE PRIMARY FIX --- !!!
  //    - Replaced AddDense() with the correct AddFullyConnected().
  //
  // static tflite::MicroMutableOpResolver<5> micro_op_resolver;
  // if (micro_op_resolver.AddConv2D() != kTfLiteOk) { MicroPrintf("Failed to add Conv2D"); return; }
  // if (micro_op_resolver.AddMaxPool2D() != kTfLiteOk) { MicroPrintf("Failed to add MaxPool2D"); return; }
  // if (micro_op_resolver.AddReshape() != kTfLiteOk) { MicroPrintf("Failed to add Reshape"); return; }
  // if (micro_op_resolver.AddFullyConnected() != kTfLiteOk) { MicroPrintf("Failed to add FullyConnected"); return; }
  // if (micro_op_resolver.AddSoftmax() != kTfLiteOk) { MicroPrintf("Failed to add Softmax"); return; }
  // if (micro_op_resolver.AddRelu() != kTfLiteOk) { MicroPrintf("Failed to add Relu"); return; }
  // Serial.println("Operators resolved successfully.");

  static tflite::AllOpsResolver all_ops_resolver;
  Serial.println("DEBUG: Using AllOpsResolver.");

  // 3. Build the interpreter.
  // static tflite::MicroInterpreter static_interpreter(model, micro_op_resolver, tensor_arena, kTensorArenaSize);
  static tflite::MicroInterpreter static_interpreter(model, all_ops_resolver, tensor_arena, kTensorArenaSize);

  interpreter = &static_interpreter;

  // 4. Allocate memory for the model's tensors in the arena.
  //    This is the most common point of failure after the OpResolver.
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    MicroPrintf("FATAL: AllocateTensors() failed. Your kTensorArenaSize might be too small.");
    return;
  }
  Serial.println("Tensor Arena allocated successfully.");

  // 5. Get pointers to the model's input and output tensors.
  model_input = interpreter->input(0);
  model_output = interpreter->output(0);
  
  // 6. Validate the input tensor to make sure it matches our expectations.
  if ((model_input->dims->size != 4) ||
      (model_input->dims->data[0] != 1) ||
      (model_input->dims->data[1] != kSequenceLength) ||
      (model_input->dims->data[2] != kNumFeatures) ||
      (model_input->dims->data[3] != 1) ||
      (model_input->type != kTfLiteFloat32)) {
    MicroPrintf("FATAL: Bad input tensor parameters in model");
    return;
  }
  Serial.println("Input tensor validated successfully.");
  
  Serial.println("\n--- Initialization Complete. Ready for inference. ---");
}

//
// --- Main Arduino `loop()` function (No changes needed here) ---
//
void loop() {
  float ax, ay, az, gx, gy, gz;

  if (IMU.accelerationAvailable() && IMU.gyroscopeAvailable()) {
    IMU.readAcceleration(ax, ay, az);
    IMU.readGyroscope(gx, gy, gz);

    // Pipeline Stage 1, 2, and 3 are identical to before...
    // The inference_buffer is filled with 50*6 scaled float values.
    
    // Add to history for cleaning
    int current_idx = history_buffer_idx * kNumFeatures;
    sensor_history_buffer[current_idx + 0] = ax; sensor_history_buffer[current_idx + 1] = ay; sensor_history_buffer[current_idx + 2] = az;
    sensor_history_buffer[current_idx + 3] = gx; sensor_history_buffer[current_idx + 4] = gy; sensor_history_buffer[current_idx + 5] = gz;
    history_buffer_idx = (history_buffer_idx + 1) % SENSOR_HISTORY_LENGTH;

    // Update cleaning parameters
    EstimateGravityDirection(); UpdateVelocity(ax, ay, az); EstimateGyroscopeDrift();

    // Clean, scale, and add to inference buffer
    int inference_idx = inference_samples_collected * kNumFeatures;
    inference_buffer[inference_idx + 0] = (ax - current_gravity[0] - SCALER_MEAN[0]) / SCALER_STD[0];
    inference_buffer[inference_idx + 1] = (ay - current_gravity[1] - SCALER_MEAN[1]) / SCALER_STD[1];
    inference_buffer[inference_idx + 2] = (az - current_gravity[2] - SCALER_MEAN[2]) / SCALER_STD[2];
    inference_buffer[inference_idx + 3] = (gx - current_gyroscope_drift[0] - SCALER_MEAN[3]) / SCALER_STD[3];
    inference_buffer[inference_idx + 4] = (gy - current_gyroscope_drift[1] - SCALER_MEAN[4]) / SCALER_STD[4];
    inference_buffer[inference_idx + 5] = (gz - current_gyroscope_drift[2] - SCALER_MEAN[5]) / SCALER_STD[5];
    inference_samples_collected++;

    // Run inference when the buffer is full
    if (inference_samples_collected == kSequenceLength) {
      // The TFLite interpreter knows the input tensor is 4D. It will correctly
      // interpret this flat buffer as a 1x50x6x1 tensor. No change is needed here.
      for (int i = 0; i < kSequenceLength * kNumFeatures; ++i) {
        model_input->data.f[i] = inference_buffer[i];
      }
      
      if (interpreter->Invoke() != kTfLiteOk) {
        MicroPrintf("Invoke failed!");
        
        inference_samples_collected = 0; 
        return;
      }

      int max_score_index = -1;
      float max_score = 0.0f;
      for (int i = 0; i < kNumClasses; ++i) {
        if (model_output->data.f[i] > max_score) {
          max_score = model_output->data.f[i];
          max_score_index = i;
        }
      }

      Serial.print("Prediction: ");
      Serial.print(GESTURE_LABELS[max_score_index]);
      Serial.print(" (");
      Serial.print(max_score * 100, 2);
      Serial.println("%)");
      
      inference_samples_collected = 0;
    }
  }
}

// NOTE: You will need to add the dummy implementations of the cleaning functions
// back in, as they were removed for brevity in this response.