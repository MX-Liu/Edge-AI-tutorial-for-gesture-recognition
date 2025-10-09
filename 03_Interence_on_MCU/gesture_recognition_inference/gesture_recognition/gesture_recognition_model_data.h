/* Copyright 2024 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

/*
 * This file contains the declarations for the gesture recognition model.
 * It is intended to be included by the main C++ application.
 */

#ifndef TENSORFLOW_LITE_MICRO_EXAMPLES_GESTURE_RECOGNITION_MODEL_DATA_H_
#define TENSORFLOW_LITE_MICRO_EXAMPLES_GESTURE_RECOGNITION_MODEL_DATA_H_

// The 'extern' keyword tells the compiler that these variables are defined
// in another file (in our case, gesture_recognition_model_data.cpp). This allows any file
// that includes this header to use these variables without causing a linker error.
extern const unsigned char g_gesture_recognition_model_data[];
extern const int g_gesture_recognition_model_data_len;

#endif // TENSORFLOW_LITE_MICRO_EXAMPLES_GESTURE_RECOGNITION_MODEL_DATA_H_
